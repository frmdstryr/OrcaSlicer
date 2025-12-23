#pragma once

#include "libslic3r/Color.hpp"
#include <array>
#include <map>
#include <wx/string.h>
#include "nlohmann/json.hpp"

namespace Slic3r {

class PresetBundle;

struct QidiBoxSlotState {
    bool present = false;
    bool runout = false;
    std::string id;
    std::string type;
    std::string vendor;
    ColorRGB color;
};

struct Qidi {
    std::string printer_ip;
    std::string api_key = "";
    std::string ca_file = "";
    uint8_t box_count = 0;
    bool box_enabled = false;
    std::array<QidiBoxSlotState, 16> box_slots;
    std::map<int, std::string> box_filamap;
    std::map<int, ColorRGB> box_colormap;

    // Sends curl request to read QidiBox state from the state_variables.cfg file
    bool fetch_filament_colordict(wxString& msg);
    bool fetch_box_state(wxString& msg);

    void sync_filament_list(PresetBundle* bundle);


};

} // end namespace Slic3r
