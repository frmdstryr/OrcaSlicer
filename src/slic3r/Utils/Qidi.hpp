#pragma once

#include <array>
#include <wx/string.h>
#include "nlohmann/json.hpp"

namespace Slic3r {

struct QidiBoxSlotState {
    bool present = false;
    bool runout = false;
    std::string filament_id;
    std::string filament_color;
    std::string filament_vendor;
};

struct Qidi {
    std::string printer_ip;
    std::string api_key = "";
    std::string ca_file = "";
    uint8_t box_count = 0;
    std::array<QidiBoxSlotState, 16> box_slots;

    // Sends curl request to read QidiBox state from the state_variables.cfg file
    bool load(wxString& msg);

    // Generate json used by the DevManager to simulate a network push
    std::string sync();

};

} // end namespace Slic3r
