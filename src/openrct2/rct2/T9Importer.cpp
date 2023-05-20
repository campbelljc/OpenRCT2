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
#include <string>
#include <sstream>
#include <vector>
#include <iterator>

template <typename Out>
void split(const std::string &s, char delim, Out result) {
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, delim)) {
        *result++ = item;
    }
}

std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, std::back_inserter(elems));
    return elems;
}
/*#include <boost/lexical_cast.hpp>

template <typename T>
void Convert(const std::string& source, T& target)
{
    target = boost::lexical_cast<T>(source);
}

template <>
void Convert(const std::string& source, int8_t& target)
{
    int value = boost::lexical_cast<int>(source);

    if(value < std::numeric_limits<int8_t>::min() || value > std::numeric_limits<int8_t>::max())
    {
        //handle error
    }
    else
    {
        target = (int8_t)value;
    }
}*/

namespace RCT2
{
    static std::mutex _objectLookupMutex;

    /**
     * Class to import RollerCoaster Tycoon 2 track designs (*.TD9).
     */
    class TD9Importer final : public ITrackImporter
    {
    private:
        OpenRCT2::MemoryStream _stream;
        std::string _name;
		std::string _path;

    public:
        TD9Importer()
        {
        }

        bool Load(const utf8* path) override
        {
            const auto extension = Path::GetExtension(path);
            if (String::Equals(extension, ".td9", true))
            {
				_path = path;
                _name = GetNameFromTrackPath(path);
                auto fs = OpenRCT2::FileStream("/Applications/Games/RollerCoaster Tycoon 2.app/Contents/Resources/drive_c/Program Files/RollerCoaster Tycoon 2/Utilities/tracksorter/Tracks/Miner's revenge.TD6", OpenRCT2::FILE_MODE_OPEN);
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
			
			///new
			
			std::ifstream in(_path);
			std::string str;
			std::vector<std::string> lines;
			while (std::getline(in, str))
			{
				//std::cout<<str<<"\n";
			    if(str.size() > 0)
			        lines.push_back(str);
			}
			in.close();
			
			short type2;
			std::vector<short> conv_lines;
			int k = 0;
			for (std::string line : lines)
			{
				const char *c = line.c_str();
				sscanf(c, "%hi", &type2);	
				conv_lines.push_back(type2);	
				k += 1;
				//std::cout<<type2<<":"<<k<<"\n";
				if (k == 33)
					break;		
			}
			//std::cout<<"T1:"<<td->type<<"\n";
			td->type = (uint8_t)(conv_lines[0]);
			
			/*td->type = (uint8_t)(conv_lines[0]);
			td->vehicle_type = (uint8_t)(conv_lines[1]);
			td->cost = (uint8_t)(conv_lines[2]);
			td->flags = (uint8_t)(conv_lines[3]);
			td->ride_mode = (RideMode)(conv_lines[4]);
			td->track_flags = (uint8_t)(conv_lines[5]);
			td->colour_scheme = (uint8_t)(conv_lines[6]);
			td->entrance_style = (uint8_t)(conv_lines[7]);
			td->total_air_time = (uint8_t)(conv_lines[8]);
			td->depart_flags = (uint8_t)(conv_lines[9]);
			td->number_of_trains = (uint8_t)(conv_lines[10]);
			td->number_of_cars_per_train = (uint8_t)(conv_lines[11]);
			td->min_waiting_time = (uint8_t)(conv_lines[12]);
			td->max_waiting_time = (uint8_t)(conv_lines[13]);
			td->operation_setting = (uint8_t)(conv_lines[14]);
			td->max_speed = (uint8_t)(conv_lines[15]);
			td->average_speed = (uint8_t)(conv_lines[16]);
			td->ride_length = (uint8_t)(conv_lines[17]);
			td->max_positive_vertical_g = (uint8_t)(conv_lines[18]);
			td->max_negative_vertical_g = (uint8_t)(conv_lines[19]);
			td->max_lateral_g = (uint8_t)(conv_lines[20]);
			//td->holes_inversions = (uint8_t)(conv_lines[21]);
			td->drops = (uint8_t)(conv_lines[22]);
			td->highest_drop_height = (uint8_t)(conv_lines[23]);
			td->excitement = (uint8_t)(conv_lines[24]);
			td->intensity = (uint8_t)(conv_lines[25]);
			td->nausea = (uint8_t)(conv_lines[26]);
			td->upkeep_cost = (uint8_t)(conv_lines[27]);
			td->flags2 = (uint8_t)(conv_lines[28]);
			rct_object_entry entry{};
			entry.flags = (uint8_t)(conv_lines[29]);
			td->vehicle_object = ObjectEntryDescriptor(entry);
			td->space_required_x = 255; //(uint8_t)(conv_lines[30]);
			td->space_required_y = 255; //(uint8_t)(conv_lines[31]);
			td->lift_hill_speed = (uint8_t)(conv_lines[32]);
			td->num_circuits = (uint8_t)(conv_lines[33]);*/
			td->name = "Test";

			int ent = 0;
			for (size_t j = 34; j < lines.size(); j ++)
			{
				if (lines[j] == "ENT")
				{
					ent = j;
					break;
				}
			}
			
			for (size_t j = 34; j < ent; j ++)
			{ // track elements
				//std::cout<<"track elem " << j << "\n";				
                rct_td46_track_element t6TrackElement{};
				
				// split at comma then convert to uint8_t
				size_t pos = 0;
				std::string token;
				std::string s = lines[j];
				pos = s.find(',');
				if (pos == std::string::npos)
				{
					//assert(false);
					continue;
				}
			    token = s.substr(0, pos);
				// convert token
				const char *c = token.c_str();
				short type;
				sscanf(c, "%hi", &type);	
				t6TrackElement.type = (uint8_t)type;
				
				s.erase(0, pos + 1);
				// convert s
				const char *d = s.c_str();
				sscanf(d, "%hi", &type);
				t6TrackElement.flags = (uint8_t)type;	
								
                //_stream.SetPosition(_stream.GetPosition() - 1);
                //_stream.Read(&t6TrackElement, sizeof(rct_td46_track_element));
                
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
			bool isExit = false;
			for (size_t j = ent; j < lines.size(); j ++)
			{
				ent = j;
				if (lines[j] == "ENT")
					continue;
				if (lines[j] == "SCEN")
					break;
                TrackDesignEntranceElement entranceElement{};
                
				std::vector<std::string> vals = split(lines[j], ',');
				//std::cout<<"val0:"<<vals[0]<<"\n";
				
				//size_t pos = 0;
				//std::string token;
				//std::string s = lines[j];
				//pos = s.find(',');
			    //token = s.substr(0, pos);
				// convert token
				const char *c = vals[0].c_str();
				signed char type;
				sscanf(c, "%hhi", &type);	
				
				//entranceElement.z = (int8_t)type;
				entranceElement.z = (uint8_t)type;
				//entranceElement.z = (entranceElement.z == -128) ? -1 : entranceElement.z;
				
				//s.erase(0, pos + 1);
				//pos = s.find(',');
			    //token = s.substr(0, pos);
				
				// convert s
				c = vals[1].c_str();
				unsigned char type2;
				//sscanf(c, "%hhu", &type2);
				sscanf(c, "%hi", &type2);
				entranceElement.direction = (uint8_t)type2;	
				uint8_t dir_cpy = (uint8_t)type2;
                //entranceElement.direction = entranceElement.direction & 0x7F;
				
				//s.erase(0, pos + 1);
				//pos = s.find(',');
			    //token = s.substr(0, pos);
				
				// convert s
				c = vals[2].c_str();
				short type3;
				sscanf(c, "%hi", &type3);
				//entranceElement.x = (int16_t)type3;	
				entranceElement.x = (uint8_t)type3;	
				
				//s.erase(0, pos + 1);
				//pos = s.find(',');
			    //token = s.substr(0, pos);
				
				// convert s
				c = vals[3].c_str();
				short type4;
				sscanf(c, "%hi", &type4);
				//entranceElement.y = (int16_t)type4;	
				entranceElement.y = (uint8_t)type4;	
				
                //entranceElement.isExit = dir_cpy >> 7;
				
				//entranceElement.z = (t6EntranceElement.z == -128) ? -1 : t6EntranceElement.z;
                //entranceElement.direction = t6EntranceElement.direction & 0x7F;
                //entranceElement.x = t6EntranceElement.x;
                //entranceElement.y = t6EntranceElement.y;
				
				
                entranceElement.isExit = isExit; // t6EntranceElement.direction >> 7;
				isExit = !isExit;
                td->entrance_elements.push_back(entranceElement);
			}
			for (size_t j = ent; j < lines.size(); j ++)
			{
				if (lines[j] == "SCEN")
					continue;
				                
				std::vector<std::string> vals = split(lines[j], ',');
				
                //TD6SceneryElement t6SceneryElement{};
                //_stream.Read(&t6SceneryElement, sizeof(TD6SceneryElement));
                TrackDesignSceneryElement sceneryElement{};
                //sceneryElement.scenery_object = ObjectEntryDescriptor(t6SceneryElement.scenery_object);
				
				const char *c = vals[0].c_str();
				unsigned char type;
				sscanf(c, "%hhi", &type);				
                sceneryElement.loc.x = (int8_t)type * COORDS_XY_STEP;
				
				c = vals[1].c_str();
				unsigned char type2;
				sscanf(c, "%hhi", &type2);
                sceneryElement.loc.y = (int8_t)type2 * COORDS_XY_STEP;
				
				c = vals[2].c_str();
				unsigned char type3;
				sscanf(c, "%hhi", &type3);				
                sceneryElement.loc.z = (int8_t)type3 * COORDS_Z_STEP;

				c = vals[3].c_str();
				unsigned char type4;
				sscanf(c, "%hhi", &type4);				
                sceneryElement.flags = (uint8_t)type4;
				
				rct_object_entry entry{};
				c = vals[4].c_str();
				unsigned char type5;
				sscanf(c, "%hhi", &type5);				
				entry.flags = (uint8_t)type5;
				
				if (vals[5][vals[5].size()-1] == ' ' || vals[5][vals[5].size()-1] == '\n')
				{
					vals[5].pop_back();
				}
				
				entry.SetName(vals[5]);
				//strcpy(entry.name, vals[5].c_str());
				
				std::cout<<"name:<"<<entry.GetName()<<">\n";
				
				sceneryElement.scenery_object = ObjectEntryDescriptor(entry);
				
                //sceneryElement.primary_colour = t6SceneryElement.primary_colour;
                //sceneryElement.secondary_colour = t6SceneryElement.secondary_colour;
                td->scenery_elements.push_back(std::move(sceneryElement));
				
				/*      rct_object_entry scenery_object; // 0x00 ? 
				        int8_t x;                        // 0x10
				        int8_t y;                        // 0x11
				        int8_t z;                        // 0x12
				        uint8_t flags;                   // 0x13 direction quadrant tertiary colour
				        uint8_t primary_colour;          // 0x14
				        uint8_t secondary_colour;        // 0x15
				*/
			}
			
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
            td->depart_flags = td6.depart_flags;
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
				std::cout<<"unsupp\n";
                log_error("Unsupported track design.");
                return nullptr;
            }

            td->operation_setting = std::min(td->operation_setting, GetRideTypeDescriptor(td->type).OperatingSettings.MaxValue);

            /*if (td->type == RIDE_TYPE_MAZE)
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
            {*/
            rct_td46_track_element t6TrackElement{};
            for (uint8_t endFlag = _stream.ReadValue<uint8_t>(); endFlag != 0xFF; endFlag = _stream.ReadValue<uint8_t>())
            {
                _stream.SetPosition(_stream.GetPosition() - 1);
                _stream.Read(&t6TrackElement, sizeof(rct_td46_track_element));
                //TrackDesignTrackElement trackElement{};

                //track_type_t trackType = RCT2TrackTypeToOpenRCT2(t6TrackElement.type, td->type, true);
                //if (trackType == TrackElemType::InvertedUp90ToFlatQuarterLoopAlias)
                //{
                //    trackType = TrackElemType::MultiDimInvertedUp90ToFlatQuarterLoop;
				//}

                //trackElement.type = trackType;
                //trackElement.flags = t6TrackElement.flags;
                //td->track_elements.push_back(trackElement);
            }
			/*
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
            }*/
			
			/*}

            for (uint8_t endFlag = _stream.ReadValue<uint8_t>(); endFlag != 0xFF; endFlag = _stream.ReadValue<uint8_t>())
            {
                _stream.SetPosition(_stream.GetPosition() - 1);
                TD6SceneryElement t6SceneryElement{};
                _stream.Read(&t6SceneryElement, sizeof(TD6SceneryElement));
                TrackDesignSceneryElement sceneryElement{};
                sceneryElement.scenery_object = ObjectEntryDescriptor(t6SceneryElement.scenery_object);
                sceneryElement.loc.x = t6SceneryElement.x * COORDS_XY_STEP;
                sceneryElement.loc.y = t6SceneryElement.y * COORDS_XY_STEP;
                sceneryElement.loc.z = t6SceneryElement.z * COORDS_Z_STEP;
                sceneryElement.flags = t6SceneryElement.flags;
                sceneryElement.primary_colour = t6SceneryElement.primary_colour;
                sceneryElement.secondary_colour = t6SceneryElement.secondary_colour;
                td->scenery_elements.push_back(std::move(sceneryElement));
            }

            td->name = _name;*/

			//std::cout<<"T2:"<<td->type<<"\n";

            UpdateRideType(td);
			//std::cout<<"T3:"<<td->type<<"\n";

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

std::unique_ptr<ITrackImporter> TrackImporter::CreateTD9()
{
    return std::make_unique<RCT2::TD9Importer>();
}
