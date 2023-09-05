#define ZMQ_BUILD_DRAFT_API
#include <zmq.hpp>

#include "ride/TrackDesignRepository.h"
#include "actions/ClearAction.h"
#include "actions/TrackDesignAction.h"
#include "actions/RideSetStatusAction.h"
#include "actions/RideDemolishAction.h"

#include "json/json.h"

/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "GameState.h"

#include "Context.h"
#include "Editor.h"
#include "Game.h"
#include "GameState.h"
#include "GameStateSnapshots.h"
#include "Input.h"
#include "OpenRCT2.h"
#include "ReplayManager.h"
#include "actions/GameAction.h"
#include "config/Config.h"
#include "entity/EntityRegistry.h"
#include "entity/PatrolArea.h"
#include "entity/Staff.h"
#include "interface/Screenshot.h"
#include "localisation/Date.h"
#include "localisation/Localisation.h"
#include "management/NewsItem.h"
#include "network/network.h"
#include "platform/Platform.h"
#include "profiling/Profiling.h"
#include "ride/Vehicle.h"
#include "scenario/Scenario.h"
#include "scripting/ScriptEngine.h"
#include "title/TitleScreen.h"
#include "title/TitleSequencePlayer.h"
#include "ui/UiContext.h"
#include "windows/Intent.h"
#include "world/Climate.h"
#include "world/MapAnimation.h"
#include "world/Park.h"
#include "world/Scenery.h"

#include <algorithm>
#include <chrono>

using namespace OpenRCT2;
using namespace OpenRCT2::Scripting;
using namespace std::chrono_literals;

// initialize the zmq context with a single IO thread
zmq::context_t context_gs{1};

// construct a REP (reply) socket and bind to interface
zmq::socket_t socket_gs{context_gs, zmq::socket_type::server};
bool firstRun = true;

GameState::GameState()
{
    _park = std::make_unique<Park>();
}

/**
 * Initialises the map, park etc. basically all S6 data.
 */
void GameState::InitAll(const TileCoordsXY& mapSize)
{
    PROFILED_FUNCTION();

    gInMapInitCode = true;
    gCurrentTicks = 0;

    map_init(mapSize);
    _park->Initialise();
    finance_init();
    banner_init();
    ride_init_all();
    ResetAllEntities();
    UpdateConsolidatedPatrolAreas();
    date_reset();
    climate_reset(ClimateType::CoolAndWet);
    News::InitQueue();

    gInMapInitCode = false;

    gNextGuestNumber = 1;

    context_init();
    scenery_set_default_placement_configuration();

    auto intent = Intent(INTENT_ACTION_CLEAR_TILE_INSPECTOR_CLIPBOARD);
    context_broadcast_intent(&intent);

    load_palette();

    CheatsReset();
    ClearRestrictedScenery();

#ifdef ENABLE_SCRIPTING
    auto& scriptEngine = GetContext()->GetScriptEngine();
    scriptEngine.ClearParkStorage();
#endif
	if (firstRun) //socket_gs.handle() == nullptr)
	{
		firstRun = false;
		std::cout<<"GS BIND\n";
	    socket_gs.bind("tcp://*:5555");		
	}
}

/**
 * Function will be called every GAME_UPDATE_TIME_MS.
 * It has its own loop which might run multiple updates per call such as
 * when operating as a client it may run multiple updates to catch up with the server tick,
 * another influence can be the game speed setting.
 */
void GameState::Tick()
{
    PROFILED_FUNCTION();

    gInUpdateCode = true;

    // Normal game play will update only once every GAME_UPDATE_TIME_MS
    uint32_t numUpdates = 1;

    // 0x006E3AEC // screen_game_process_mouse_input();
    screenshot_check();
    game_handle_keyboard_input();

    if (game_is_not_paused() && gPreviewingTitleSequenceInGame)
    {
        auto player = GetContext()->GetUiContext()->GetTitleSequencePlayer();
        if (player != nullptr)
        {
            player->Update();
        }
    }

    network_update();

    if (network_get_mode() == NETWORK_MODE_CLIENT && network_get_status() == NETWORK_STATUS_CONNECTED
        && network_get_authstatus() == NetworkAuth::Ok)
    {
        numUpdates = std::clamp<uint32_t>(network_get_server_tick() - gCurrentTicks, 0, 10);
    }
    else
    {
        // Determine how many times we need to update the game
        if (gGameSpeed > 1)
        {
            // Update more often if game speed is above normal.
            numUpdates = 1 << (gGameSpeed - 1);
        }
    }

    bool isPaused = game_is_paused();
    if (network_get_mode() == NETWORK_MODE_SERVER && gConfigNetwork.pause_server_if_no_clients)
    {
        // If we are headless we always have 1 player (host), pause if no one else is around.
        if (gOpenRCT2Headless && network_get_num_players() == 1)
        {
            isPaused |= true;
        }
    }

    bool didRunSingleFrame = false;
    if (isPaused)
    {
        if (gDoSingleUpdate && network_get_mode() == NETWORK_MODE_NONE)
        {
            didRunSingleFrame = true;
            pause_toggle();
            numUpdates = 1;
        }
        else
        {
            // NOTE: Here are a few special cases that would be normally handled in UpdateLogic.
            // If the game is paused it will not call UpdateLogic at all.
            numUpdates = 0;

            if (network_get_mode() == NETWORK_MODE_SERVER)
            {
                // Make sure the client always knows about what tick the host is on.
                network_send_tick();
            }

            // Update the animation list. Note this does not
            // increment the map animation.
            map_animation_invalidate_all();

            // Post-tick network update
            network_process_pending();

            // Post-tick game actions.
            GameActions::ProcessQueue();
        }
    }

    // Update the game one or more times
    for (uint32_t i = 0; i < numUpdates; i++)
    {
        UpdateLogic();
        if (gGameSpeed == 1)
        {
            if (input_get_state() == InputState::Reset || input_get_state() == InputState::Normal)
            {
                if (input_test_flag(INPUT_FLAG_VIEWPORT_SCROLLING))
                {
                    input_set_flag(INPUT_FLAG_VIEWPORT_SCROLLING, false);
                    break;
                }
            }
            else
            {
                break;
            }
        }
    }

    network_flush();

    if (!gOpenRCT2Headless)
    {
        input_set_flag(INPUT_FLAG_VIEWPORT_SCROLLING, false);

        // the flickering frequency is reduced by 4, compared to the original
        // it was done due to inability to reproduce original frequency
        // and decision that the original one looks too fast
        if (gCurrentRealTimeTicks % 4 == 0)
            gWindowMapFlashingFlags ^= MapFlashingFlags::SwitchColour;

        // Handle guest map flashing
        gWindowMapFlashingFlags &= ~MapFlashingFlags::FlashGuests;
        if (gWindowMapFlashingFlags & MapFlashingFlags::GuestListOpen)
            gWindowMapFlashingFlags |= MapFlashingFlags::FlashGuests;
        gWindowMapFlashingFlags &= ~MapFlashingFlags::GuestListOpen;

        // Handle staff map flashing
        gWindowMapFlashingFlags &= ~MapFlashingFlags::FlashStaff;
        if (gWindowMapFlashingFlags & MapFlashingFlags::StaffListOpen)
            gWindowMapFlashingFlags |= MapFlashingFlags::FlashStaff;
        gWindowMapFlashingFlags &= ~MapFlashingFlags::StaffListOpen;

        context_update_map_tooltip();

        context_handle_input();
    }

    // Always perform autosave check, even when paused
    if (!(gScreenFlags & SCREEN_FLAGS_TITLE_DEMO) && !(gScreenFlags & SCREEN_FLAGS_TRACK_DESIGNER)
        && !(gScreenFlags & SCREEN_FLAGS_TRACK_MANAGER))
    {
        scenario_autosave_check();
    }

    window_dispatch_update_all();

    if (didRunSingleFrame && game_is_not_paused() && !(gScreenFlags & SCREEN_FLAGS_TITLE_DEMO))
    {
        pause_toggle();
    }

    gDoSingleUpdate = false;
    gInUpdateCode = false;
}


std::array<CoordsXY, 6> possibleCoords = {
	CoordsXY(6320, 6832),
	CoordsXY(4220, 1350),
	CoordsXY(0, 0),
	CoordsXY(150, 150),
	CoordsXY(200, 200),
	CoordsXY(350, 350)
};

RideId createdRideID = RideId::GetNull();
void GameState::UpdateLogic(LogicTimings* timings)
{
    zmq::message_t request;
	
    //const CoordsXY mapCoords = ViewportInteractionGetTileStartAtCursor(screenCoords);
	//std::cout<<mapCoords.x<<","<<mapCoords.y<<"\n";
	
    gBankLoan = 0.00_GBP;
	gResearchFundingLevel = 0;

    // receive a request from client
    auto num = socket_gs.recv(request, zmq::recv_flags::dontwait);
	if (num > 0)
	{
		Json::Value res;
		Json::Reader reader = {};
		std::cout<<"received "<<request.to_string()<<"\n";
		auto status = reader.parse(request.to_string(), res);
        if (!status) {
            std::cout <<"ConHash:: Error while parsing: " << reader.getFormattedErrorMessages().c_str() << std::endl;
        } else {
            std::cout<<"Successfully parsed !!" <<std::endl;
			
			window_close_all();
			if (res["action"] == "load_track")
			{
				std::string path = res["path"].asString();
				const char* path_c = path.c_str();
				
				if (!(createdRideID.IsNull()))
		        {
					auto ride = get_ride(createdRideID);
					std::cout<<"R1\n";
					createdRideID = RideId::GetNull();
			
				    // close first...
					auto gameAction1 = RideSetStatusAction(ride->id, RideStatus::Closed);
				    //gameAction.SetCallback([&](const GameAction*, const GameActions::Result* result) {
				    //});
				    gameAction1.SetFlags(GAME_COMMAND_FLAG_APPLY);
			
				    GameActions::ExecuteNested(&gameAction1);
					std::cout<<"R2\n";
				    if (!(ride->lifecycle_flags & RIDE_LIFECYCLE_ON_TRACK))
					{
						ride->lifecycle_flags &= RIDE_LIFECYCLE_ON_TRACK;
					}
				    ride_clear_for_construction(ride);
			
					auto gameAction2 = RideDemolishAction(ride->id, RIDE_MODIFY_DEMOLISH);
				    gameAction2.SetFlags(GAME_COMMAND_FLAG_APPLY);
				    //gameAction.SetCallback([&](const GameAction*, const GameActions::Result* result) {
					std::cout<<"R3\n";
					//});				
				    GameActions::ExecuteNested(&gameAction2);
			


				    ClearableItems itemsToClear = 0;
			        itemsToClear |= CLEARABLE_ITEMS::SCENERY_SMALL;
			        itemsToClear |= CLEARABLE_ITEMS::SCENERY_LARGE;
			        itemsToClear |= CLEARABLE_ITEMS::SCENERY_FOOTPATH;
			
					auto mapSizeMaxXY = GetMapSizeMaxXY();
				    auto range = MapRange(0, 0, mapSizeMaxXY.x, mapSizeMaxXY.y);

				    auto cAction = ClearAction(range, itemsToClear);
					auto res = GameActions::Execute(&cAction);
			
					//ride_action_modify(&ride, RIDE_MODIFY_DEMOLISH, GAME_COMMAND_FLAG_APPLY);
		        }
		
			    std::unique_ptr<TrackDesign> _trackDesign = TrackDesignImport(path_c);
				if (_trackDesign != nullptr)
				{
					_trackDesign->name = "Test";
	
					RideId _rideIndex{ RideId::GetNull() };
		
					auto _currentTrackPieceDirection = static_cast<Direction>(0);

	
					CoordsXYZ trackLoc;
					GameActions::Result res;
					bool found2 = false;
					for (auto &mapCoords : possibleCoords)
					{
					    auto surfaceElement = map_get_surface_element_at(mapCoords);
					    auto mapZ = surfaceElement->GetBaseZ() + TrackDesignGetZPlacement(_trackDesign.get(), GetOrAllocateRide(_rideIndex), { mapCoords, surfaceElement->GetBaseZ() });
						//std::cout<<"mapz:"<<mapZ<<"\n";
		
					    trackLoc = { mapCoords, mapZ };
						bool found = false;
					    for (int32_t i2 = 0; i2 < 7; i2++, trackLoc.z += 8)
					    {
					        auto tdAction = TrackDesignAction(CoordsXYZD{ trackLoc.x, trackLoc.y, trackLoc.z, _currentTrackPieceDirection }, *_trackDesign);
					        tdAction.SetFlags(0);
					        res = GameActions::Query(&tdAction);

					        // If successful don't keep trying.
					        // If failure due to no money then increasing height only makes problem worse
					        if (res.Error != GameActions::Status::Ok) // || res.Error == GameActions::Status::InsufficientFunds)
					        {
								std::cout<<"ERR1\n" << res.GetErrorMessage()<<"\n";
					        }
							else
							{
								found = true;
								break;
							}
					    }
						if (found)
						{
							found2 = true;
							break;
						}
					}
	
					if (!found2)
					{
						std::cout<<"stopping\n";
						return;
					}
					std::cout<<"Placing\n";
				    auto tdAction = TrackDesignAction({ trackLoc, _currentTrackPieceDirection }, *_trackDesign);
				    tdAction.SetCallback([&](const GameAction*, const GameActions::Result* result) {
				        if (result->Error == GameActions::Status::Ok)
				        {
				            auto rideId = result->GetData<RideId>();
				            auto getRide = get_ride(rideId);
							createdRideID = rideId;
				            if (getRide != nullptr)
				            {
				                //auto intent = Intent(WC_RIDE);
				                //intent.putExtra(INTENT_EXTRA_RIDE_ID, rideId.ToUnderlying());
				                //context_open_intent(&intent);
				
							    RideSetStatusAction gameAction = RideSetStatusAction(rideId, RideStatus::Open);
								gameAction.SetCallback([&](const GameAction*, const GameActions::Result* result) {
									std::cout<<"CALLBACK\n";
								});
							    GameActions::ExecuteNested(&gameAction);
				            }
				        }
				        else
				        {
							std::cout<<"ERR2\n";
						}
				    });
				    res = GameActions::Execute(&tdAction);
		
				    // send the reply to the client
					//std::cout << "Sending back.\n";
				    //socket.send(zmq::buffer(data), zmq::send_flags::none);
				}
			}
        }
	}
	else
	{
		if (!(createdRideID.IsNull()))
        {
			auto ride = get_ride(createdRideID);
			if (ride->custom_name == "Test done")
			{
				auto gameAction1 = RideSetStatusAction(ride->id, RideStatus::Closed);
			    gameAction1.SetFlags(GAME_COMMAND_FLAG_APPLY);			
			    GameActions::ExecuteNested(&gameAction1);
				ride->custom_name = "Test done2";
			}
		}
	}
	
	
    PROFILED_FUNCTION();

    auto start_time = std::chrono::high_resolution_clock::now();

    auto report_time = [timings, start_time](LogicTimePart part) {
        if (timings != nullptr)
        {
            timings->TimingInfo[part][timings->CurrentIdx] = std::chrono::high_resolution_clock::now() - start_time;
        }
    };

    gScreenAge++;
    if (gScreenAge == 0)
        gScreenAge--;

    GetContext()->GetReplayManager()->Update();

    network_update();
    report_time(LogicTimePart::NetworkUpdate);

    if (network_get_mode() == NETWORK_MODE_SERVER)
    {
        if (network_gamestate_snapshots_enabled())
        {
            CreateStateSnapshot();
        }

        // Send current tick out.
        network_send_tick();
    }
    else if (network_get_mode() == NETWORK_MODE_CLIENT)
    {
        // Don't run past the server, this condition can happen during map changes.
        if (network_get_server_tick() == gCurrentTicks)
        {
            return;
        }

        // Check desync.
        bool desynced = network_check_desynchronisation();
        if (desynced)
        {
            // If desync debugging is enabled and we are still connected request the specific game state from server.
            if (network_gamestate_snapshots_enabled() && network_get_status() == NETWORK_STATUS_CONNECTED)
            {
                // Create snapshot from this tick so we can compare it later
                // as we won't pause the game on this event.
                CreateStateSnapshot();

                network_request_gamestate_snapshot();
            }
        }
    }

#ifdef ENABLE_SCRIPTING
    // Stash the current day number before updating the date so that we
    // know if the day number changes on this tick.
    auto day = _date.GetDay();
#endif

    date_update();
    _date = Date(static_cast<uint32_t>(gDateMonthsElapsed), gDateMonthTicks);
    report_time(LogicTimePart::Date);

    scenario_update();
    report_time(LogicTimePart::Scenario);
    climate_update();
    report_time(LogicTimePart::Climate);
    map_update_tiles();
    report_time(LogicTimePart::MapTiles);
    // Temporarily remove provisional paths to prevent peep from interacting with them
    map_remove_provisional_elements();
    report_time(LogicTimePart::MapStashProvisionalElements);
    map_update_path_wide_flags();
    report_time(LogicTimePart::MapPathWideFlags);
    peep_update_all();
    report_time(LogicTimePart::Peep);
    map_restore_provisional_elements();
    report_time(LogicTimePart::MapRestoreProvisionalElements);
    vehicle_update_all();
    report_time(LogicTimePart::Vehicle);
    UpdateAllMiscEntities();
    report_time(LogicTimePart::Misc);
    Ride::UpdateAll();
    report_time(LogicTimePart::Ride);

    if (!(gScreenFlags & SCREEN_FLAGS_EDITOR))
    {
        _park->Update(_date);
    }
    report_time(LogicTimePart::Park);

    research_update();
    report_time(LogicTimePart::Research);
    ride_ratings_update_all();
    report_time(LogicTimePart::RideRatings);
    ride_measurements_update();
    report_time(LogicTimePart::RideMeasurments);
    News::UpdateCurrentItem();
    report_time(LogicTimePart::News);

    map_animation_invalidate_all();
    report_time(LogicTimePart::MapAnimation);
    vehicle_sounds_update();
    peep_update_crowd_noise();
    climate_update_sound();
    report_time(LogicTimePart::Sounds);
    editor_open_windows_for_current_step();

    // Update windows
    // window_dispatch_update_all();

    // Start autosave timer after update
    if (gLastAutoSaveUpdate == AUTOSAVE_PAUSE)
    {
        gLastAutoSaveUpdate = Platform::GetTicks();
    }

    GameActions::ProcessQueue();
    report_time(LogicTimePart::GameActions);

    network_process_pending();
    network_flush();
    report_time(LogicTimePart::NetworkFlush);

    gCurrentTicks++;
    gSavedAge++;

#ifdef ENABLE_SCRIPTING
    auto& hookEngine = GetContext()->GetScriptEngine().GetHookEngine();
    hookEngine.Call(HOOK_TYPE::INTERVAL_TICK, true);

    if (day != _date.GetDay())
    {
        hookEngine.Call(HOOK_TYPE::INTERVAL_DAY, true);
    }
    report_time(LogicTimePart::Scripts);
#endif

    if (timings != nullptr)
    {
        timings->CurrentIdx = (timings->CurrentIdx + 1) % LOGIC_UPDATE_MEASUREMENTS_COUNT;
    }
}

void GameState::CreateStateSnapshot()
{
    PROFILED_FUNCTION();

    IGameStateSnapshots* snapshots = GetContext()->GetGameStateSnapshots();

    auto& snapshot = snapshots->CreateSnapshot();
    snapshots->Capture(snapshot);
    snapshots->LinkSnapshot(snapshot, gCurrentTicks, scenario_rand_state().s0);
}
