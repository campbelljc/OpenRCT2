/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "../TrackImporter.h"
#include "../config/Config.h"
#include "../core/FileStream.h"
#include "../core/MemoryStream.h"
#include "../core/Path.hpp"
#include "../core/String.hpp"
#include "../object/ObjectRepository.h"
#include "../object/RideObject.h"
#include "../rct12/SawyerChunkReader.h"
#include "../rct12/SawyerEncoding.h"
#include "../ride/Ride.h"
#include "../ride/RideData.h"
#include "../ride/TrackDesign.h"
#include "../ride/TrackDesignRepository.h"

#include <mutex>
#include <iostream>

uint8_t firstDepartFlag = 0;
namespace RCT2
{
    static std::mutex _objectLookupMutex;

    /**
     * Class to import RollerCoaster Tycoon 2 track designs (*.TD6).
     */
    class TD6Importer final : public ITrackImporter
    {
    private:
        OpenRCT2::MemoryStream _stream;
        std::string _name;

    public:
        TD6Importer()
        {
        }

        bool Load(const utf8* path) override
        {
            const auto extension = Path::GetExtension(path);
            if (String::Equals(extension, ".td6", true))
            {
                _name = GetNameFromTrackPath(path);
                auto fs = OpenRCT2::FileStream(path, OpenRCT2::FILE_MODE_OPEN);
                return LoadFromStream(&fs);
            }

            throw std::runtime_error("Invalid RCT2 track extension.");
        }

        bool LoadFromStream(OpenRCT2::IStream* stream) override
        {
            if (!gConfigGeneral.allow_loading_with_incorrect_checksum
                && SawyerEncoding::ValidateTrackChecksum(stream) != RCT12TrackDesignVersion::TD6)
            {
                throw IOException("Invalid checksum.");
            }

            auto chunkReader = SawyerChunkReader(stream);
            auto data = chunkReader.ReadChunkTrack();
            _stream.WriteArray<const uint8_t>(reinterpret_cast<const uint8_t*>(data->GetData()), data->GetLength());
            _stream.SetPosition(0);
            return true;
        }

        std::unique_ptr<TrackDesign> Import() override
        {
            std::unique_ptr<TrackDesign> td = std::make_unique<TrackDesign>();

            TD6Track td6{};
            // Rework td6 so that it is just the fields
            _stream.Read(&td6, 0xA3);

            td->type = td6.type; // 0x00
            td->vehicle_type = td6.vehicle_type;

            td->cost = 0;
            td->flags = td6.flags;
            td->ride_mode = static_cast<RideMode>(td6.ride_mode);
            td->track_flags = 0;
            td->colour_scheme = td6.version_and_colour_scheme & 0x3;
            for (auto i = 0; i < Limits::MaxTrainsPerRide; ++i)
            {
                td->vehicle_colours[i] = td6.vehicle_colours[i];
                td->vehicle_additional_colour[i] = td6.vehicle_additional_colour[i];
            }
            td->entrance_style = td6.entrance_style;
            td->total_air_time = td6.total_air_time;
			if (firstDepartFlag == 0)
			{
				firstDepartFlag = td6.depart_flags;
			}
            td->depart_flags = firstDepartFlag;
			//std::cout<<"Depart flags:" << td->depart_flags<<"\n";
            td->number_of_trains = td6.number_of_trains;
            td->number_of_cars_per_train = td6.number_of_cars_per_train;
            td->min_waiting_time = td6.min_waiting_time;
            td->max_waiting_time = td6.max_waiting_time;
            td->operation_setting = td6.operation_setting;
            td->max_speed = td6.max_speed;
            td->average_speed = td6.average_speed;
            td->ride_length = td6.ride_length;
            td->max_positive_vertical_g = td6.max_positive_vertical_g;
            td->max_negative_vertical_g = td6.max_negative_vertical_g;
            td->max_lateral_g = td6.max_lateral_g;

            if (td->type == RIDE_TYPE_MINI_GOLF)
            {
                td->holes = td6.holes;
            }
            else
            {
                td->inversions = td6.inversions;
            }

            td->drops = td6.drops;
            td->highest_drop_height = td6.highest_drop_height;
            td->excitement = td6.excitement;
            td->intensity = td6.intensity;
            td->nausea = td6.nausea;
            td->upkeep_cost = td6.upkeep_cost;
            for (auto i = 0; i < Limits::NumColourSchemes; ++i)
            {
                td->track_spine_colour[i] = td6.track_spine_colour[i];
                td->track_rail_colour[i] = td6.track_rail_colour[i];
                td->track_support_colour[i] = td6.track_support_colour[i];
            }
            td->flags2 = td6.flags2;
            td->vehicle_object = ObjectEntryDescriptor(td6.vehicle_object);
            td->space_required_x = td6.space_required_x;
            td->space_required_y = td6.space_required_y;
            td->lift_hill_speed = td6.lift_hill_speed_num_circuits & 0b00011111;
            td->num_circuits = td6.lift_hill_speed_num_circuits >> 5;

            auto version = static_cast<RCT12TrackDesignVersion>((td6.version_and_colour_scheme >> 2) & 3);
            if (version != RCT12TrackDesignVersion::TD6)
            {
                log_error("Unsupported track design.");
                return nullptr;
            }

            td->operation_setting = std::min(td->operation_setting, GetRideTypeDescriptor(td->type).OperatingSettings.MaxValue);

            if (td->type == RIDE_TYPE_MAZE)
            {
                rct_td46_maze_element t6MazeElement{};
                t6MazeElement.all = !0;
                while (t6MazeElement.all != 0)
                {
                    _stream.Read(&t6MazeElement, sizeof(rct_td46_maze_element));
                    if (t6MazeElement.all != 0)
                    {
                        TrackDesignMazeElement mazeElement{};
                        mazeElement.x = t6MazeElement.x;
                        mazeElement.y = t6MazeElement.y;
                        mazeElement.direction = t6MazeElement.direction;
                        mazeElement.type = t6MazeElement.type;
                        td->maze_elements.push_back(mazeElement);
                    }
                }
            }
            else
            {
                rct_td46_track_element t6TrackElement{};
                for (uint8_t endFlag = _stream.ReadValue<uint8_t>(); endFlag != 0xFF; endFlag = _stream.ReadValue<uint8_t>())
                {
                    _stream.SetPosition(_stream.GetPosition() - 1);
                    _stream.Read(&t6TrackElement, sizeof(rct_td46_track_element));
                    TrackDesignTrackElement trackElement{};

                    track_type_t trackType = RCT2TrackTypeToOpenRCT2(t6TrackElement.type, td->type, true);
                    if (trackType == TrackElemType::InvertedUp90ToFlatQuarterLoopAlias)
                    {
                        trackType = TrackElemType::MultiDimInvertedUp90ToFlatQuarterLoop;
                    }

                    trackElement.type = trackType;
                    trackElement.flags = t6TrackElement.flags;
                    td->track_elements.push_back(trackElement);
                }

                TD6EntranceElement t6EntranceElement{};
                for (uint8_t endFlag = _stream.ReadValue<uint8_t>(); endFlag != 0xFF; endFlag = _stream.ReadValue<uint8_t>())
                {
                    _stream.SetPosition(_stream.GetPosition() - 1);
                    _stream.Read(&t6EntranceElement, sizeof(TD6EntranceElement));
                    TrackDesignEntranceElement entranceElement{};
                    entranceElement.z = (t6EntranceElement.z == -128) ? -1 : t6EntranceElement.z;
                    entranceElement.direction = t6EntranceElement.direction & 0x7F;
                    entranceElement.x = t6EntranceElement.x;
                    entranceElement.y = t6EntranceElement.y;
                    entranceElement.isExit = t6EntranceElement.direction >> 7;
                    td->entrance_elements.push_back(entranceElement);
                }
            }

            for (uint8_t endFlag = _stream.ReadValue<uint8_t>(); endFlag != 0xFF; endFlag = _stream.ReadValue<uint8_t>())
            {
                _stream.SetPosition(_stream.GetPosition() - 1);
                TD6SceneryElement t6SceneryElement{};
                _stream.Read(&t6SceneryElement, sizeof(TD6SceneryElement));
                TrackDesignSceneryElement sceneryElement{};
                sceneryElement.scenery_object = ObjectEntryDescriptor(t6SceneryElement.scenery_object);
				std::cout<<t6SceneryElement.x<<","<<t6SceneryElement.y<<","<<t6SceneryElement.z<<","<<t6SceneryElement.flags<<"---"<<t6SceneryElement.scenery_object.flags<<","<<t6SceneryElement.scenery_object.GetName()<<"\n";
                sceneryElement.loc.x = t6SceneryElement.x * COORDS_XY_STEP;
                sceneryElement.loc.y = t6SceneryElement.y * COORDS_XY_STEP;
                sceneryElement.loc.z = t6SceneryElement.z * COORDS_Z_STEP;
                sceneryElement.flags = t6SceneryElement.flags;
                sceneryElement.primary_colour = t6SceneryElement.primary_colour;
                sceneryElement.secondary_colour = t6SceneryElement.secondary_colour;
                td->scenery_elements.push_back(std::move(sceneryElement));
            }

            td->name = _name;

            UpdateRideType(td);
			
			
			// now save...
			std::string fname = "export_" + td->name + ".td6";
			std::cout<<fname <<"\n";
			std::ofstream myfile;
			myfile.open(fname);
			
            myfile << static_cast<short>(td->type) << "\n"; // 0x00
            myfile << static_cast<short>(td->vehicle_type) << "\n";

            myfile << static_cast<short>(td->cost) << "\n";
            myfile << static_cast<short>(td->flags) << "\n";
            myfile << static_cast<short>(td->ride_mode) << "\n";
            myfile << static_cast<short>(td->track_flags) << "\n";
            myfile << static_cast<short>(td->colour_scheme) << "\n";
            /*for (auto i = 0 << "\n"; i < Limits::MaxTrainsPerRide << "\n"; ++i)
            {
                td->vehicle_colours[i] = td6.vehicle_colours[i] << "\n";
                td->vehicle_additional_colour[i] = td6.vehicle_additional_colour[i] << "\n";
            }*/
            myfile << static_cast<short>(td->entrance_style) << "\n";
            myfile << static_cast<short>(td->total_air_time) << "\n";
            myfile << static_cast<short>(td->depart_flags) << "\n";
            myfile << static_cast<short>(td->number_of_trains) << "\n";
            myfile << static_cast<short>(td->number_of_cars_per_train) << "\n";
            myfile << static_cast<short>(td->min_waiting_time) << "\n";
            myfile << static_cast<short>(td->max_waiting_time) << "\n";
            myfile << static_cast<short>(td->operation_setting) << "\n";
            myfile << static_cast<short>(td->max_speed) << "\n"; // int8_t
            myfile << static_cast<short>(td->average_speed) << "\n"; // int8_t
            myfile << static_cast<short>(td->ride_length) << "\n";
            myfile << static_cast<short>(td->max_positive_vertical_g) << "\n";
            myfile << static_cast<short>(td->max_negative_vertical_g) << "\n"; // int8_t
            myfile << static_cast<short>(td->max_lateral_g) << "\n";

            if (td->type == RIDE_TYPE_MINI_GOLF)
            {
                myfile << static_cast<short>(td->holes) << "\n";
            }
            else
            {
                myfile << static_cast<short>(td->inversions) << "\n";
            }

            myfile << static_cast<short>(td->drops) << "\n";
            myfile << static_cast<short>(td->highest_drop_height) << "\n";
            myfile << static_cast<short>(td->excitement) << "\n";
            myfile << static_cast<short>(td->intensity) << "\n";
            myfile << static_cast<short>(td->nausea) << "\n";
            myfile << static_cast<short>(td->upkeep_cost) << "\n";
            /*for (auto i = 0 << "\n"; i < Limits::NumColourSchemes << "\n"; ++i)
            {
                td->track_spine_colour[i] = td6.track_spine_colour[i] << "\n";
                td->track_rail_colour[i] = td6.track_rail_colour[i] << "\n";
                td->track_support_colour[i] = td6.track_support_colour[i] << "\n";
            }*/
            myfile << static_cast<short>(td->flags2) << "\n";
            //myfile << static_cast<short>(td->vehicle_object) << "\n";
            myfile << static_cast<unsigned int>(td6.vehicle_object.flags) << "\n";
            //myfile << td->vehicle_object << "\n";
            myfile << static_cast<short>(td->space_required_x) << "\n";
            myfile << static_cast<short>(td->space_required_y) << "\n";
            myfile << static_cast<short>(td->lift_hill_speed) << "\n";
            myfile << static_cast<short>(td->num_circuits) << "\n";
			
			myfile << static_cast<short>(td->track_elements.size()) << "\n";
			for(TrackDesignTrackElement el : td->track_elements)
			{
				myfile << static_cast<short>(el.type) << "," << static_cast<short>(el.flags) << "\n";
			}
			myfile << "ENT\n";
			for(TrackDesignEntranceElement el : td->entrance_elements)
			{
				myfile<< static_cast<short>(el.z) <<","<<static_cast<short>(el.direction)<<","<<static_cast<short>(el.x)<<","<<static_cast<short>(el.y) << "\n";
			}
			myfile << "SCEN\n";
			for(auto el : td->scenery_elements)
			{
				myfile<< static_cast<short>(el.loc.x/COORDS_XY_STEP) <<","<<static_cast<short>(el.loc.y/COORDS_XY_STEP)<<","<<static_cast<short>(el.loc.z/COORDS_Z_STEP)<<","<<static_cast<short>(el.flags)<<","<<static_cast<unsigned int>(el.scenery_object.Entry.flags)<<","<<el.scenery_object.Entry.GetName() << "\n";				
			}
			
			myfile.close();

            return td;
        }

        void UpdateRideType(std::unique_ptr<TrackDesign>& td)
        {
            if (RCT2RideTypeNeedsConversion(td->type))
            {
                std::scoped_lock<std::mutex> lock(_objectLookupMutex);
                auto rawObject = object_repository_load_object(&td->vehicle_object.Entry);
                if (rawObject != nullptr)
                {
                    const auto* rideEntry = static_cast<const rct_ride_entry*>(
                        static_cast<RideObject*>(rawObject.get())->GetLegacyData());
                    if (rideEntry != nullptr)
                    {
                        td->type = RCT2RideTypeToOpenRCT2RideType(td->type, rideEntry);
                    }
                    rawObject->Unload();
                }
            }
        }
    };
} // namespace RCT2

std::unique_ptr<ITrackImporter> TrackImporter::CreateTD6()
{
    return std::make_unique<RCT2::TD6Importer>();
}
