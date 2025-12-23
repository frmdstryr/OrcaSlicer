#include "Qidi.hpp"
#include "Http.hpp"
#include <nlohmann/json.hpp>
#include <regex>
#include "libslic3r/PresetBundle.hpp"

using namespace nlohmann;

namespace Slic3r {

bool Qidi::fetch_filament_colordict(wxString& msg)
{
    const std::regex section_pattern("\\s*\\[\\s*(.+)\\s*\\]\\s*");
    const std::regex fila_pattern("fila(\\d+)");
    const std::regex colordict_pattern("(\\d+)\\s*=\\s*#([A-F0-9]{6})\\s*");
    const std::regex filament_pattern("filament\\s*=\\s*(.+)\\s*");
    bool result = false;

    const std::string url = (boost::format("http://%s/server/files/config/officiall_filas_list.cfg") % printer_ip).str();
    BOOST_LOG_TRIVIAL(info) << boost::format("Qidi: Loading filament list from %s") % url;
    auto http = Http::get(url);
    if (!api_key.empty())
        http.header("X-Api-Key", api_key);
    if (!ca_file.empty())
        http.ca_file(ca_file);
    http.timeout_connect(4)
    .on_error([&](std::string body, std::string error, unsigned status) {
        result = false;
        const auto m = boost::format("Qidi: Error loading filament list: %1%, HTTP %2%, body: `%3%`") % error % status % body;
        BOOST_LOG_TRIVIAL(error) << m;
        msg = m.str();
    })
    .on_complete([&](std::string body, unsigned) {
        try {
            std::istringstream stream(body);
            std::string current_section;
            int filament_index = -1;
            for (std::string line; std::getline(stream, line);) {
                std::smatch m;
                if (std::regex_match(line, m, section_pattern)) {
                    assert(m.size() == 2);
                    current_section = m[1].str();
                    BOOST_LOG_TRIVIAL(debug) << boost::format("Qidi: section: %s") % current_section;

                    // [fila\d+] section
                    if (std::regex_match(current_section, m, fila_pattern)) {
                        assert(m.size() == 2);
                        filament_index = std::stoi(m[1].str());
                    } else {
                        filament_index = -1;
                    }
                }
                else if (std::regex_match(line, m, filament_pattern) && filament_index > 0) {
                    assert(m.size() == 2);
                    const auto filament = m[1].str();
                    BOOST_LOG_TRIVIAL(debug) << boost::format("Qidi: filament name: %1% = %2%") % filament_index % filament;
                    box_filamap[filament_index] = filament;
                }
                else if (std::regex_match(line, m, colordict_pattern) && current_section == "colordict") {
                    assert(m.size() == 3);
                    const auto i = std::stoi(m[1].str());
                    const auto color = m[2].str();
                    BOOST_LOG_TRIVIAL(debug) << boost::format("Qidi: filament color %1% = #%2%") % i % color;
                    assert(color.size() == 6);
                    box_colormap[i] = ColorRGB(
                        static_cast<unsigned char>(std::stoi(color.substr(0, 2), nullptr, 16)),
                        static_cast<unsigned char>(std::stoul(color.substr(2, 2), nullptr, 16)),
                        static_cast<unsigned char>(std::stoul(color.substr(4, 2), nullptr, 16))
                    );
                    result = true;
                }
            }
        }
        catch (const std::exception& e) {
            msg = wxString::Format("Error parsing config values: %s", e.what());
        }
    })
    .perform_sync();
    BOOST_LOG_TRIVIAL(debug) << boost::format("Qidi: Loading filament list result %s") % result;
    return result;
}


bool Qidi::fetch_box_state(wxString& msg)
{
    bool result = false;
    const std::string url = (boost::format("http://%s/printer/objects/query?save_variables&box_extras&aht20_f+heater_box1") % printer_ip).str();
    BOOST_LOG_TRIVIAL(info) << boost::format("Qidi: Loading printer state from %s") % url;
    auto http = Http::get(url);
    if (!api_key.empty())
        http.header("X-Api-Key", api_key);
    if (!ca_file.empty())
        http.ca_file(ca_file);
    http.timeout_connect(4)
    .on_error([&](std::string body, std::string error, unsigned status) {
        result = false;
        const auto m = boost::format("Qidi: Error loading box state: %1%, HTTP %2%, body: `%3%`") % error % status % body;
        BOOST_LOG_TRIVIAL(error) << m;
        msg = m.str();
    })
    .on_complete([&](std::string body, unsigned) {
        try {
            json response = json::parse(body);
            if (!response.contains("result") || !response["result"].contains("status")) {
                return;
            }
            json printer_status = response["result"]["status"];
            if (!printer_status.contains("save_variables")) {
                return;
            }
            result = true;
            json vars = printer_status["save_variables"];
            if (vars.contains("box_count") && vars["box_count"].is_number_unsigned()) {
                box_count = vars["box_count"].get<uint8_t>();
            }
            if (vars.contains("enable_box") && vars["enable_box"].is_number_unsigned()) {
                box_enabled = vars["enable_box"].get<uint8_t>() == 1;
            }
            BOOST_LOG_TRIVIAL(debug) << boost::format("Qidi: box_enabled=%u box_count=%u") % box_enabled % box_count;

            for (int i=0; i<box_slots.size(); i++) {
                QidiBoxSlotState &slot = box_slots[i];
                const std::string slot_key = (boost::format("slot%u") % i).str();
                if (vars.contains(slot_key) && vars[slot_key].is_number_unsigned()) {
                    slot.present = vars[slot_key].get<uint8_t>() == 1;
                } else {
                    slot.present = false;
                }
                BOOST_LOG_TRIVIAL(debug) << boost::format("Qidi: box_slot[%u].present=%u") % i % slot.present;
                if (!slot.present)
                    continue;

                const std::string color_key = (boost::format("color_slot%u") % i).str();
                if (vars.contains(color_key) && vars[color_key].is_number_unsigned()) {
                    const auto j = vars[color_key].get<uint8_t>();
                    try {
                        slot.color = box_colormap[j];
                    } catch(const std::out_of_range& ex) {
                        slot.color = ColorRGB::BLACK();
                    }
                } else {
                    slot.color = ColorRGB::BLACK();
                }
                BOOST_LOG_TRIVIAL(debug) << boost::format("Qidi: box_slot[%u].color=%s") % i % encode_color(slot.color);

                const std::string filament_key = (boost::format("filament_slot%u") % i).str();
                if (vars.contains(filament_key) && vars[filament_key].is_number_unsigned()) {
                    const auto j = vars[filament_key].get<uint8_t>();
                    try {
                        slot.type = box_filamap[j];
                    } catch(const std::out_of_range& ex) {
                        slot.type = "";
                    }
                } else {
                    slot.type = "";
                }
                BOOST_LOG_TRIVIAL(debug) << boost::format("Qidi: box_slot[%u].type=%s") % i % slot.type;

                const std::string vendor_key = (boost::format("vendor_slot%u") % i).str();
                if (vars.contains(vendor_key) && vars[vendor_key].is_number_unsigned()) {
                    slot.vendor = (vars[vendor_key].get<uint8_t>() == 1) ? "Qidi" : "";
                } else {
                    slot.vendor = "";
                }
                BOOST_LOG_TRIVIAL(debug) << boost::format("Qidi: box_slot[%u].vendor=%s") % i % slot.vendor;
            }
        }
        catch (const std::exception& e) {
            msg = wxString::Format("Error parsing config values: %s", e.what());
        }
    })
    .perform_sync();
    BOOST_LOG_TRIVIAL(debug) << boost::format("Qidi: Loading box state result %s") % result;
    return result;
}

void Qidi::sync_filament_list(PresetBundle* bundle)
{
    if (!bundle || !bundle->is_qidi_vendor())
        return;
    const auto config = bundle->printers.get_edited_preset().config;
    const auto ip = config.opt_string("print_host");
    if (ip.empty())
        return;
    printer_ip = ip;
    wxString msg;
    if (!fetch_filament_colordict(msg))
        return;
    if (!fetch_box_state(msg))
        return;


}

} // end namespace Slic3r
