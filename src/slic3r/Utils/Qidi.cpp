#include "Qidi.hpp"
#include "Http.hpp"
#include <nlohmann/json.hpp>
#include <regex>

using namespace nlohmann;

namespace Slic3r {

bool Qidi::load(wxString& msg)
{
    const std::regex box_count_pattern("box_count\s*=\s*(\d+)\s*");
    const std::regex slot_present_pattern("slot(\d+)\s*=\s*(\d+)\s*");
    const std::regex slot_runout_pattern("runout_(\d+)\s*=\s*(\d+)\s*");
    const std::regex slot_color_pattern("color_slot(\d+)\s*=\s*(\.+)\s*");
    const std::regex slot_fila_pattern("filament_slot(\d+)\s*=\s*(\.+)\s*");
    const std::regex slot_vendor_pattern("vendor_slot(\d+)\s*=\s*(\.+)\s*");
    const std::string name = "Qidi";
    bool result = true;

    const std::string url = (boost::format("http://%s:7125/server/files/config/saved_variables.cfg") % printer_ip).str();
    BOOST_LOG_TRIVIAL(info) << boost::format("%s: Load %s") % name % url;
    auto http = Http::get(url);
    if (!api_key.empty())
        http.header("X-Api-Key", api_key);
    if (!ca_file.empty())
        http.ca_file(ca_file);
    http.timeout_connect(4)
    .on_error([&](std::string body, std::string error, unsigned status) {
        result = false;
        const auto m = boost::format("%1%: Error getting saved variables: %2%, HTTP %3%, body: `%4%`") % name % error % status % body;
        BOOST_LOG_TRIVIAL(error) << m;
        msg = m.str();
    })
    .on_complete([&](std::string body, unsigned) {
        try {
            std::istringstream stream(body);
            for (std::string line; std::getline(stream, line);) {
                std::smatch m;
                if (std::regex_match(line, m, box_count_pattern)) {
                    box_count = std::stoi(m[0].str());
                }
                else if (std::regex_match(line, m, slot_present_pattern)) {
                    const auto i = (std::stoi(m[0].str()) - 1) & 0xf;
                    box_slots[i].present = std::stoi(m[1].str()) == 1;
                }
                else if (std::regex_match(line, m, slot_runout_pattern)) {
                    const auto i = (std::stoi(m[0].str()) - 1) & 0xf;
                    box_slots[i].runout = std::stoi(m[1].str()) == 1;
                }
                else if (std::regex_match(line, m, slot_fila_pattern)) {
                    const auto i = (std::stoi(m[0].str()) - 1) & 0xf;
                    box_slots[i].filament_id = m[1].str();
                }
                else if (std::regex_match(line, m, slot_color_pattern)) {
                    const auto i = (std::stoi(m[0].str()) - 1) & 0xf;
                    box_slots[i].filament_color = m[1].str();
                }
                else if (std::regex_match(line, m, slot_vendor_pattern)) {
                    const auto i = (std::stoi(m[0].str()) - 1) & 0xf;
                    box_slots[i].filament_vendor = m[1].str();
                }
            }
        }
        catch (const std::exception& e) {
            msg = wxString::Format("Error parsing config values: %s", e.what());
        }
    })
    .perform_sync();
    return result;
}

std::string Qidi::sync()
{
    wxString msg;
    if (!load(msg))
        return "{}";
    json j;
    json print;
    if (box_count > 0) {
        json ams_info;
        json ams;
        uint8_t ams_exist_bits = 0;
        for (int i=0; i<box_count; i++) {
            ams_exist_bits |= (1 << i);
            json box;
            box["id"] = (boost::format("%1%") % i).str();
            box["qidi"] = 1;

            json trays;
            for (int k=0; k<4; k++) {
                json tray;
                tray["id"] = (boost::format("%1%") % k).str();
                tray["tray_color"] = box_slots[i*4+k].filament_color;
                trays.push_back(tray);
            }
            box["tray"] = trays;

            ams.push_back(box);
        }
        ams_info["ams_exist_bits"] = ams_exist_bits;
        uint8_t ams_tray_bits = 0;
        for (int i=0; i<16; i++) {
            if (box_slots[i].present) {
                ams_tray_bits |= (1 << i);
            }
        }
        ams_info["ams_tray_bits"] = ams_tray_bits;
        ams_info["ams"] = ams;
        print["ams"] = ams_info;
    }

    // Must increment m_push_count
    print["command"] = "push_status";
    print["msg"] = 0;
    j["print"] = print;
    return j.dump();
}

} // end namespace Slic3r
