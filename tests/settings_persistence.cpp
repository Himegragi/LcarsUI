#include "settings_store.hpp"
#include "check.hpp"
#include "core_internal.hpp"
#include "lcars/semantics.hpp"
#include <filesystem>
#include <fstream>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
void round_trip() {
    settings_demo::Demo demo; auto document = demo.document();
    document.name=u"Station A"; document.notices=false; document.mode=lcars::OptionValue{"focus"}; document.region=lcars::OptionValue{"south"}; document.volume=75;
    document.items[0].name=u"Changed"; document.items[0].note=u"Note"; document.items.push_back({{"T021"},u"New",u"Details"}); document.next_item_id=22;
    document.presentation.font_family=u"NoSuchFont"; document.presentation.text_size=23; document.presentation.variant=lcars::ThemeVariant::dark(); document.presentation.density=lcars::Density::compact; document.accent="green";
    auto bytes = settings_demo::write_settings(document);
    test::check(bytes.starts_with("{\n  \"format\"") && bytes.back()=='\n' && bytes.find('\r')==std::string::npos && !bytes.starts_with("\xef\xbb\xbf"), "UTF-8 without BOM uses LF and fixed first key");
    test::check(bytes.find("\"general\"") < bytes.find("\"items\"") && bytes.find("\"items\"") < bytes.find("\"next_item_id\"") && bytes.find("\"next_item_id\"") < bytes.find("\"presentation\""), "top-level keys have specified order");
    auto decoded = settings_demo::parse_settings(bytes);
    test::check(decoded && *decoded == document, "settings survive JSON round trip");
}
void multiline_notes_round_trip_and_reject_carriage_returns() {
    settings_demo::Demo demo; auto document = demo.document();
    document.items[0].note = u"\u7b2c\u4e00\u884c\n\n\u7b2c\u4e09\u884c\nlast";
    auto bytes = settings_demo::write_settings(document);
    test::check(bytes.find("\\n\\n") != std::string::npos && bytes.find('\r') == std::string::npos, "line breaks are stored as JSON \\n escapes");
    auto decoded = settings_demo::parse_settings(bytes);
    test::check(decoded && *decoded == document && decoded->items[0].note == document.items[0].note, "a note with line breaks loads back identical");
    const std::string base = R"({"format":"lcars-settings-demo","version":1,"general":{"name":"A","notices":true,"mode":"normal","region":"north","volume":40},"items":[{"id":"T005","name":"A","note":"x"}],"next_item_id":6,"presentation":{"font_family":"","text_size":16,"variant":"light","density":"standard"}})";
    auto replace = [&](std::string from, std::string to) { auto value = base; auto at = value.find(from); test::check(at != std::string::npos, "fixture field"); value.replace(at, from.size(), to); return value; };
    test::check(bool(settings_demo::parse_settings(replace("\"note\":\"x\"", "\"note\":\"a\\nb\""))), "an LF in a note is valid");
    test::check(!settings_demo::parse_settings(replace("\"note\":\"x\"", "\"note\":\"a\\rb\"")), "a CR in a note is rejected");
    test::check(!settings_demo::parse_settings(replace("\"note\":\"x\"", "\"note\":\"a\\tb\"")), "a tab in a note is rejected");
    test::check(!settings_demo::parse_settings(replace("\"name\":\"A\",\"note\"", "\"name\":\"A\\nB\",\"note\"")), "a line break in an item name is rejected");
    test::check(!settings_demo::parse_settings(replace("\"note\":\"x\"", "\"note\":\"" + std::string(201, 'x') + "\"")), "the 200 character limit still applies");
}
// #81: a note is saved as {"text", "spans"}; files written before rich text (a plain string) load unstyled.
void rich_notes_round_trip_and_old_string_notes_still_load() {
    settings_demo::Demo demo; auto document = demo.document();
    document.items[0].note = u"重要\n內容";
    document.items[0].note_spans = {{.start = 0, .length = 2, .bold = true}, {.start = 3, .length = 2, .italic = true, .underline = true, .color = "red"}};
    auto bytes = settings_demo::write_settings(document);
    test::check(bytes.find("\"text\"") != std::string::npos && bytes.find("\"spans\"") != std::string::npos && bytes.find("\"bold\": true") != std::string::npos &&
                bytes.find("\"color\": \"red\"") != std::string::npos, "a note is an object with its text and spans");
    auto decoded = settings_demo::parse_settings(bytes);
    test::check(decoded && *decoded == document && decoded->items[0].note_spans == document.items[0].note_spans, "a styled note loads back exactly");
    test::check(decoded && settings_demo::write_settings(*decoded) == bytes, "writing what was loaded gives the same file");
    const std::string base = R"({"format":"lcars-settings-demo","version":1,"general":{"name":"A","notices":true,"mode":"normal","region":"north","volume":40},"items":[{"id":"T005","name":"A","note":"x"}],"next_item_id":6,"presentation":{"font_family":"","text_size":16,"variant":"light","density":"standard"}})";
    auto with_note = [&](const std::string& note) { auto value = base; auto at = value.find("\"note\":\"x\""); test::check(at != std::string::npos, "fixture field"); value.replace(at, 10, "\"note\":" + note); return value; };
    auto old = settings_demo::parse_settings(with_note("\"plain\""));
    test::check(old && old->items[0].note == u"plain" && old->items[0].note_spans.empty(), "a note stored as a string loads with no styles");
    auto object = settings_demo::parse_settings(with_note(R"({"text":"abcd","spans":[{"start":0,"length":2,"bold":true},{"start":2,"length":2,"bold":true}]})"));
    test::check(object && object->items[0].note_spans.size() == 1 && object->items[0].note_spans[0].length == 4, "spans are loaded in their normal form (adjacent equal spans merge)");
    test::check(settings_demo::parse_settings(with_note(R"({"text":"","spans":[]})")).has_value(), "an empty note object is valid");
    for (const char* bad : {R"({"text":"abc","spans":[{"start":0,"length":2,"bold":true},{"start":1,"length":2,"italic":true}]})",  // overlap
                            R"({"text":"abc","spans":[{"start":2,"length":2,"bold":true}]})",                                         // past the text
                            R"({"text":"abc","spans":[{"start":0,"length":1,"bold":"yes"}]})",                                       // not a boolean
                            R"({"text":"abc","spans":[{"start":0,"length":1,"blink":true}]})",                                       // unknown member
                            R"({"text":"abc","spans":[{"length":1,"bold":true}]})",                                                   // no start
                            R"({"text":"a\rb","spans":[]})", R"({"text":"abc"})", R"({"spans":[]})", "5"})
        test::check(!settings_demo::parse_settings(with_note(bad)), "an invalid note object makes the file invalid");
    test::check(!settings_demo::parse_settings(with_note("{\"text\":\"" + std::string(201, 'x') + "\",\"spans\":[]}")), "the 200 character limit applies to the text of a rich note");
}
void files() {
    auto path=std::filesystem::temp_directory_path()/"lcars-persistence-test";
    std::filesystem::remove_all(path);
    settings_demo::SettingsStore store(path);
    test::check(store.load().kind==settings_demo::LoadKind::missing,"missing file");
    settings_demo::Demo demo; auto document=demo.document(); document.name=u"工作站 A";
    test::check(store.save(document).empty(),"save succeeds");
    auto loaded=store.load(); test::check(loaded.document && *loaded.document==document,"load saved settings");
    { std::ofstream f(path/"settings.json",std::ios::binary); f << "broken"; }
    auto bad=store.load(); test::check(bad.kind==settings_demo::LoadKind::corrupt && std::filesystem::exists(path/"settings.json.bad"),"corrupt file preserved");
    std::filesystem::remove_all(path);
}
void save_from_draft() {
    auto path=std::filesystem::temp_directory_path()/"lcars-persistence-draft";
    std::filesystem::remove_all(path); settings_demo::SettingsStore store(path); settings_demo::Demo demo;
    demo.on_save([&](const auto& doc) { return store.save(doc); });
    demo.name.focus(); demo.name.set_value(u"工作站 A");
    test::check(bool(lcars::detail::access(demo.ui).key_down({.ctrl=true,.letter=u'S'})), "save focused draft with Ctrl+S");
    auto loaded=store.load(); test::check(loaded.document && loaded.document->name==u"工作站 A", "draft is present in file");
    std::filesystem::remove_all(path);
}
void close_persistence() {
    auto path=std::filesystem::temp_directory_path()/"lcars-close-persistence";
    std::filesystem::remove_all(path);
    settings_demo::SettingsStore store(path); settings_demo::Demo demo;
    int closes=0; demo.on_close([&] { ++closes; });
    demo.on_save([&](const auto& document) { return store.save(document); });
    test::check(bool(demo.volume.set_value(65)) && !demo.request_close(), "close asks to save");
    test::check(bool(demo.close_save.invoke()) && closes==1 && store.load().document->volume==65, "close save writes and exits");
    auto bytes=[&] { std::ifstream file(path/"settings.json", std::ios::binary); return std::string(std::istreambuf_iterator<char>(file), {}); };
    auto before=bytes();
    test::check(bool(demo.volume.set_value(70)) && !demo.request_close(), "close asks again");
    test::check(bool(demo.close_discard.invoke()) && closes==2, "discard exits");
    auto loaded=store.load();
    test::check(loaded.document && bytes()==before, "discard leaves file bytes unchanged");
    settings_demo::Demo reopened; reopened.load_document(*loaded.document);
    test::check(reopened.document().volume==65, "reopened demo retains saved volume");
    std::filesystem::remove_all(path);
}
void validation() {
    settings_demo::Demo demo; auto good=settings_demo::write_settings(demo.document());
    auto replace=[&](std::string from,std::string to) { auto copy=good; auto pos=copy.find(from); test::check(pos!=std::string::npos,"fixture contains field"); copy.replace(pos,from.size(),to); return copy; };
    for (const auto& bytes : {replace("\"version\": 1","\"version\": 2"), replace("\"text_size\": 16","\"text_size\": 16.0"), replace("\"text_size\": 16","\"text_size\": 40"), replace("\"volume\": 40","\"volume\": 7"), replace("\"region\": \"north\"","\"region\": \"west\""), replace("\"variant\": \"light\"","\"variant\": \"high-contrast\""), replace("\"name\": \"測試使用者\"","\"name\": \"   \""), std::string(1024*1024+1,'x'), replace("\"north\"",std::string("\"\xff\"",3))})
        test::check(!settings_demo::parse_settings(bytes), "invalid field rejects whole document");
    test::check(bool(settings_demo::parse_settings(std::string("\xef\xbb\xbf")+good)), "BOM accepted");
    auto extra=replace("\"version\": 1","\"extra\": 1, \"version\": 1");
    auto decoded=settings_demo::parse_settings(extra); test::check(decoded && settings_demo::write_settings(*decoded).find("extra")==std::string::npos,"unknown fields discarded");
}
void appearance_persistence() {
    auto path=std::filesystem::temp_directory_path()/"lcars-persistence-appearance";
    std::filesystem::remove_all(path); settings_demo::SettingsStore store(path);
    settings_demo::Demo demo; demo.on_save([&](const auto& doc) { return store.save(doc); });
    auto settings=demo.ui.presentation(); settings.font_family=u"NoSuchFont"; settings.text_size=20;
    test::check(bool(demo.set_presentation(settings)) && bool(demo.save()), "save appearance settings");
    settings.text_size=28; test::check(bool(demo.set_presentation(settings)), "make unsaved size change");
    auto loaded=store.load(); test::check(bool(loaded.document), "load saved appearance");
    settings_demo::Demo reopened; reopened.load_document(*loaded.document); reopened.nav.select({"appearance"});
    test::check(reopened.ui.presentation().text_size==20, "reopen retains saved size");
    auto font=reopened.ui.semantics().get({"appearance.font",lcars::OptionValue{"family:NoSuchFont"}});
    test::check(font && font->selected && font->name==u"NoSuchFont\uff08\u672a\u5b89\u88dd\uff09", "saved missing font stays selected");
    std::filesystem::remove_all(path);
}
void accent_persistence() {
    settings_demo::Demo demo; auto document=demo.document();
    test::check(document.accent=="default" && settings_demo::write_settings(document).find("\"accent\": \"default\"")!=std::string::npos, "default accent is written");
    const std::string old_file=R"({"format":"lcars-settings-demo","version":1,"general":{"name":"A","notices":true,"mode":"normal","region":"north","volume":40},"items":[{"id":"T005","name":"A","note":""}],"next_item_id":6,"presentation":{"font_family":"","text_size":16,"variant":"light","density":"standard"}})";
    auto old=settings_demo::parse_settings(old_file); test::check(old && old->accent=="default", "a file without accent loads as default");
    const std::string density="\"density\":\"standard\"";
    auto with=[&](const std::string& value) { auto copy=old_file; copy.replace(copy.find(density),density.size(),density+",\"accent\":"+value); return copy; };
    auto orange=settings_demo::parse_settings(with("\"orange\"")); test::check(orange && orange->accent=="orange", "accent orange loads");
    for (const char* bad : {"\"purple\"","5","null","\"\""}) test::check(!settings_demo::parse_settings(with(bad)), "unknown accent rejects the whole document");
    auto path=std::filesystem::temp_directory_path()/"lcars-persistence-accent";
    std::filesystem::remove_all(path); settings_demo::SettingsStore store(path);
    settings_demo::Demo saving; saving.on_save([&](const auto& doc) { return store.save(doc); });
    test::check(bool(saving.set_accent("green")) && bool(saving.save()), "choose and save the accent");
    test::check(bool(saving.set_accent("orange")), "make an unsaved accent change");
    auto loaded=store.load(); test::check(loaded.document && loaded.document->accent=="green", "saved accent is in the file");
    settings_demo::Demo reopened; reopened.load_document(*loaded.document); reopened.nav.select({"appearance"});
    test::check(reopened.accent_id()=="green" && reopened.ui.semantics().get({"appearance.accent",lcars::OptionValue{"green"}})->selected, "reopened demo shows the saved accent");
    test::check(reopened.ui.effective_palette(lcars::web_appearance(),lcars::ThemeVariant::light())->accent==lcars::Color{20,110,50}, "reopened demo paints the saved accent");
    std::filesystem::remove_all(path);
}
// #90: the focus ring settings are saved with the presentation. Unknown or out-of-range values make the whole file corrupt (as an
// unknown accent does); a file without them is not corrupt and means the defaults.
void focus_ring_persistence() {
    settings_demo::Demo demo; auto document=demo.document();
    auto written=settings_demo::write_settings(document);
    test::check(written.find("\"focus_indicator\": \"automatic\"")!=std::string::npos && written.find("\"focus_ring_width\": null")!=std::string::npos &&
                written.find("\"focus_ring_radius\": null")!=std::string::npos && written.find("\"focus_color\": \"default\"")!=std::string::npos, "the defaults are written: automatic, empty, empty, default");
    const std::string old_file=R"({"format":"lcars-settings-demo","version":1,"general":{"name":"A","notices":true,"mode":"normal","region":"north","volume":40},"items":[{"id":"T005","name":"A","note":""}],"next_item_id":6,"presentation":{"font_family":"","text_size":16,"variant":"light","density":"standard"}})";
    auto old=settings_demo::parse_settings(old_file);
    test::check(old && old->presentation.focus_indicator==lcars::FocusIndicatorMode::automatic && !old->presentation.focus_ring_width && !old->presentation.focus_ring_radius && old->focus_color=="default",
                "a file without the four settings is not corrupt and loads the defaults");
    const std::string density="\"density\":\"standard\"";
    auto with=[&](const std::string& key, const std::string& value) { auto copy=old_file; copy.replace(copy.find(density),density.size(),density+",\""+key+"\":"+value); return copy; };
    for (auto [value,mode] : {std::pair{"\"automatic\"",lcars::FocusIndicatorMode::automatic},std::pair{"\"always\"",lcars::FocusIndicatorMode::always},std::pair{"\"never\"",lcars::FocusIndicatorMode::never}}) {
        auto loaded=settings_demo::parse_settings(with("focus_indicator",value)); test::check(loaded && loaded->presentation.focus_indicator==mode, "each indicator mode loads");
    }
    for (int width : {2,3,6}) { auto loaded=settings_demo::parse_settings(with("focus_ring_width",std::to_string(width))); test::check(loaded && loaded->presentation.focus_ring_width==width, "2-6 load as the thickness"); }
    for (int radius : {0,1,32}) { auto loaded=settings_demo::parse_settings(with("focus_ring_radius",std::to_string(radius))); test::check(loaded && loaded->presentation.focus_ring_radius==radius, "0-32 load as the radius"); }
    for (const char* key : {"focus_ring_width","focus_ring_radius"}) { auto loaded=settings_demo::parse_settings(with(key,"null")); test::check(loaded && !loaded->presentation.focus_ring_width && !loaded->presentation.focus_ring_radius, "null is the empty setting"); }
    for (const char* color : {"\"default\"","\"green\"","\"orange\""}) { auto loaded=settings_demo::parse_settings(with("focus_color",color)); test::check(loaded && loaded->focus_color==std::string(color).substr(1,std::string(color).size()-2), "each ring color loads"); }
    for (const char* bad : {"\"off\"","\"\"","1","null","true"}) test::check(!settings_demo::parse_settings(with("focus_indicator",bad)), "an unknown indicator mode rejects the whole document");
    for (const char* bad : {"1","7","0","-2","\"3\"","3.5","true","[]"}) test::check(!settings_demo::parse_settings(with("focus_ring_width",bad)), "a thickness out of range or of the wrong type rejects the whole document");
    for (const char* bad : {"-1","33","100","\"6\"","6.5","false","{}"}) test::check(!settings_demo::parse_settings(with("focus_ring_radius",bad)), "a radius out of range or of the wrong type rejects the whole document");
    for (const char* bad : {"\"purple\"","5","null","\"\"","true"}) test::check(!settings_demo::parse_settings(with("focus_color",bad)), "an unknown ring color rejects the whole document");
    auto path=std::filesystem::temp_directory_path()/"lcars-persistence-focus-ring";
    std::filesystem::remove_all(path); settings_demo::SettingsStore store(path);
    settings_demo::Demo saving; saving.on_save([&](const auto& doc) { return store.save(doc); });
    saving.nav.select({"appearance"}); auto& tree=saving.ui.semantics();
    test::check(bool(tree.perform({"appearance.focus_mode",lcars::OptionValue{"always"}},lcars::Verb::select)) && bool(tree.perform({"appearance.focus_width"},lcars::Verb::set_value,5)) &&
                bool(tree.perform({"appearance.focus_radius_same"},lcars::Verb::toggle)) && bool(tree.perform({"appearance.focus_radius"},lcars::Verb::set_value,12)) &&
                bool(tree.perform({"appearance.focus_color",lcars::OptionValue{"green"}},lcars::Verb::select)) && bool(saving.save()), "choose all four and save");
    test::check(bool(tree.perform({"appearance.focus_mode",lcars::OptionValue{"never"}},lcars::Verb::select)), "make an unsaved change");
    auto loaded=store.load();
    test::check(loaded.document && loaded.document->presentation.focus_indicator==lcars::FocusIndicatorMode::always && loaded.document->presentation.focus_ring_width==5 &&
                loaded.document->presentation.focus_ring_radius==12 && loaded.document->focus_color=="green", "the file has the saved values");
    settings_demo::Demo reopened; reopened.load_document(*loaded.document); reopened.nav.select({"appearance"});
    auto& again=reopened.ui.semantics();
    test::check(again.get({"appearance.focus_mode",lcars::OptionValue{"always"}})->selected && again.get({"appearance.focus_width"})->numeric_value==5 && again.get({"appearance.focus_radius"})->numeric_value==12 &&
                *again.get({"appearance.focus_radius_same"})->checked==false && again.get({"appearance.focus_color",lcars::OptionValue{"green"}})->selected, "the reopened demo shows them on the page");
    test::check(reopened.ui.presentation().focus_ring_width==5 && reopened.ui.effective_palette(lcars::web_appearance(),lcars::ThemeVariant::light())->focus==lcars::Color{20,110,50}, "and applies them");
    std::filesystem::remove_all(path);
}
// #84: the "justified text as left" setting is saved with the presentation and comes back on load.
void justify_as_left_persistence() {
    settings_demo::Demo demo; auto document=demo.document();
    test::check(!document.presentation.justify_as_left && settings_demo::write_settings(document).find("\"justify_as_left\": false")!=std::string::npos, "the default is written as off");
    const std::string old_file=R"({"format":"lcars-settings-demo","version":1,"general":{"name":"A","notices":true,"mode":"normal","region":"north","volume":40},"items":[{"id":"T005","name":"A","note":""}],"next_item_id":6,"presentation":{"font_family":"","text_size":16,"variant":"light","density":"standard"}})";
    auto old=settings_demo::parse_settings(old_file); test::check(old && !old->presentation.justify_as_left, "a file without the setting loads as off");
    const std::string density="\"density\":\"standard\"";
    auto with=[&](const std::string& value) { auto copy=old_file; copy.replace(copy.find(density),density.size(),density+",\"justify_as_left\":"+value); return copy; };
    auto on=settings_demo::parse_settings(with("true")); test::check(on && on->presentation.justify_as_left, "the setting loads as on");
    for (const char* bad : {"\"yes\"","1","null"}) test::check(!settings_demo::parse_settings(with(bad)), "a non-boolean rejects the whole document");
    auto path=std::filesystem::temp_directory_path()/"lcars-persistence-justify";
    std::filesystem::remove_all(path); settings_demo::SettingsStore store(path);
    settings_demo::Demo saving; saving.on_save([&](const auto& doc) { return store.save(doc); });
    saving.nav.select({"appearance"});
    test::check(bool(saving.ui.semantics().perform({"appearance.justify_left"}, lcars::Verb::toggle)) && bool(saving.save()), "turn it on and save");
    test::check(bool(saving.ui.semantics().perform({"appearance.justify_left"}, lcars::Verb::toggle)), "turn it off again, unsaved");
    auto loaded=store.load(); test::check(loaded.document && loaded.document->presentation.justify_as_left, "the saved file has it on");
    settings_demo::Demo reopened; reopened.load_document(*loaded.document); reopened.nav.select({"appearance"});
    test::check(reopened.ui.presentation().justify_as_left && *reopened.ui.semantics().get({"appearance.justify_left"})->checked, "the reopened demo applies it and shows the checkbox on");
    std::filesystem::remove_all(path);
}
void failures();
void load_failures();
void order();
void native_files();
void dialog_failures();
void edge_documents();
int main() { const test::Case cases[] = {{"close persistence",close_persistence}, {"appearance persistence",appearance_persistence}, {"accent persistence",accent_persistence}, {"focus ring persistence",focus_ring_persistence}, {"justify as left persistence",justify_as_left_persistence}, {"settings round trip", round_trip},{"multiline notes",multiline_notes_round_trip_and_reject_carriage_returns},{"rich notes",rich_notes_round_trip_and_old_string_notes_still_load},{"files",files},{"save draft",save_from_draft},{"validation",validation},{"failures",failures},{"load failures",load_failures},{"save order",order},{"native files",native_files},{"dialog failures",dialog_failures},{"edge documents",edge_documents}}; return test::run("settings persistence", cases); }
void failures() {
    auto path=std::filesystem::temp_directory_path()/"lcars-persistence-fail";
    std::filesystem::remove_all(path); settings_demo::Demo demo; settings_demo::SettingsStore initial(path);
    test::check(initial.save(demo.document()).empty(),"initial save");
    auto original=settings_demo::write_settings(demo.document());
    for (auto step : {"directory","create","write","flush","close","move"}) {
        settings_demo::SettingsStore store(path,[&](std::string_view name) { return name==step ? (name=="write" ? 112u : 5u) : 0u; });
        auto changed=demo.document(); changed.volume=45;
        test::check(!store.save(changed).empty(),"injected failure reported");
        std::ifstream f(path/"settings.json",std::ios::binary); std::string bytes((std::istreambuf_iterator<char>(f)),{});
        test::check(bytes==original,"original bytes preserved"); f.close();
        for (const auto& entry : std::filesystem::directory_iterator(path))
            test::check(entry.path().extension()!=L".tmp", "failed save removes temporary file");
    }
    int attempts=0; settings_demo::SettingsStore retry(path,[&](std::string_view name) { return name=="move" && ++attempts<=2 ? 32u : 0u; });
    test::check(retry.save(demo.document()).empty() && attempts==3,"transient move retries");
    std::filesystem::remove_all(path);
}
void load_failures() {
    auto path=std::filesystem::temp_directory_path()/"lcars-persistence-load";
    std::filesystem::remove_all(path); std::filesystem::create_directories(path);
    { std::ofstream f(path/"settings.json",std::ios::binary); f << "broken"; }
    settings_demo::SettingsStore denied(path,[](std::string_view step) { return step=="read" ? 5u : 0u; });
    test::check(denied.load().kind==settings_demo::LoadKind::unreadable && std::filesystem::exists(path/"settings.json"),"unreadable stays in place");
    settings_demo::SettingsStore rename_denied(path,[](std::string_view step) { return step=="rename" ? 5u : 0u; });
    auto failed=rename_denied.load(); test::check(failed.kind==settings_demo::LoadKind::corrupt && !failed.renamed && failed.reason.find(u"格式或欄位不合法") != std::u16string::npos && failed.reason.find(u"原檔無法改名") != std::u16string::npos && failed.reason.back() != u'\n' && failed.reason.back() != u'\r',"corruption and rename failure reported without trailing newline");
    { std::ofstream old(path/"settings.json.bad",std::ios::binary); old << "old"; }
    settings_demo::SettingsStore normal(path); auto result=normal.load();
    std::ifstream f(path/"settings.json.bad",std::ios::binary); std::string bytes((std::istreambuf_iterator<char>(f)),{});
    test::check(result.renamed && bytes=="broken","bad file replaces old backup without losing bytes");
    f.close(); std::filesystem::remove_all(path);
}
void order() {
    auto path=std::filesystem::temp_directory_path()/"lcars-persistence-order";
    std::filesystem::remove_all(path); std::vector<std::string> calls;
    settings_demo::SettingsStore store(path,[&](std::string_view step) { calls.emplace_back(step); return 0u; });
    settings_demo::Demo demo; test::check(store.save(demo.document()).empty(),"save for sequence");
    test::check(calls==std::vector<std::string>{"directory","create","write","flush","close","move"},"save stages in order");
    std::filesystem::remove_all(path);
}
void native_files() {
    auto path=std::filesystem::temp_directory_path()/L"測試 資料夾"/L"設定";
    std::filesystem::remove_all(path.parent_path()); settings_demo::SettingsStore store(path); settings_demo::Demo demo;
    test::check(store.save(demo.document()).empty(),"Unicode directory saves");
    auto file=store.path();
    auto handle=CreateFileW(file.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    test::check(handle!=INVALID_HANDLE_VALUE && store.load().kind==settings_demo::LoadKind::unreadable,"exclusive read denied without renaming");
    test::check(!store.save(demo.document()).empty(),"locked destination refuses replacement"); CloseHandle(handle);
    test::check(SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_READONLY)!=0,"set read only");
    test::check(!store.save(demo.document()).empty(),"read-only destination refuses replacement"); SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_NORMAL);
    std::filesystem::remove_all(path.parent_path());
    auto obstruction=std::filesystem::temp_directory_path()/"lcars-store-obstruction";
    { std::ofstream f(obstruction); f << 'x'; }
    settings_demo::SettingsStore invalid(obstruction/"child"); test::check(!invalid.save(demo.document()).empty(),"file in directory path fails");
    std::filesystem::remove(obstruction);
}
void dialog_failures() {
    settings_demo::Demo demo; auto& state=lcars::detail::access(demo.ui);
    demo.on_save([](const auto&) { return std::u16string(u"disk full"); },u"settings.json");
    demo.notices.toggle();
    test::check(!state.invoke("app.save"),"save failure returned");
    auto message=state.describe("app.error_dialog.message");
    test::check(message && message->value.find(u"disk full")!=std::u16string::npos,"error dialog contains reason");
    test::check(!demo.ui.semantics().get({"app.error_dialog.ok"})->hidden,"save error dialog shown");
    test::check(bool(state.invoke("app.error_dialog.ok")) && state.describe("app.save")->enabled,"failure leaves document dirty");
    demo.show_load_notice(u"corrupt",u"renamed");
    test::check(!demo.ui.semantics().get({"app.load_dialog.ok"})->hidden,"load dialog shown");
    test::check(bool(state.invoke("app.load_dialog.ok")) && demo.nav.describe()->focused,"load dialog returns focus to navigation");
}
void edge_documents() {
    const std::string base=R"({"format":"lcars-settings-demo","version":1,"general":{"name":"A","notices":true,"mode":"normal","region":"north","volume":40},"items":[{"id":"T005","name":"A","note":""}],"next_item_id":6,"presentation":{"font_family":"NoSuchFont","text_size":16,"variant":"light","density":"standard"}})";
    auto good=settings_demo::parse_settings(base); test::check(good && good->presentation.font_family==u"NoSuchFont","uninstalled family retained");
    auto mutate=[&](std::string from,std::string to) { auto value=base; auto at=value.find(from); test::check(at!=std::string::npos,"edge fixture field"); value.replace(at,from.size(),to); return value; };
    for (const auto& value : {mutate("{\"id\":\"T005\",\"name\":\"A\",\"note\":\"\"}","{\"id\":\"T005\",\"name\":\"A\",\"note\":\"\"},{\"id\":\"T005\",\"name\":\"B\",\"note\":\"\"}"), mutate("\"next_item_id\":6","\"next_item_id\":5"), mutate("\"presentation\":{","\"ignored\":{"), mutate("\"name\":\"A\"","\"name\":\"  \""), mutate("\"format\":\"lcars-settings-demo\"",std::string("\"extra\":\"\xff\",\"format\":\"lcars-settings-demo\""))})
        test::check(!settings_demo::parse_settings(value),"edge document rejected");
}
