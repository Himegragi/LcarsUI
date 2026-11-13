# V02 list and scroll example

The tabbed showcase and this standalone example assemble the same V02 page, model, and list behavior. The list has a bounded inner viewport inside the scrolling page; keyboard navigation reveals the target through both viewports.

This standalone page uses the same application model and cases with the Minimal Web and Material-inspired appearances. Data is in memory.

Build from the repository root with `./build.ps1 -Configuration Release`, then run `build/Release/lcars_list_page_web.exe` or `build/Release/lcars_list_page_material.exe`. Choose the 0, 1, 20, or 100 item sample; choosing a sample resets this page. Search filters names, reverse changes identity order, and the T008 button toggles that row's availability. Delete acts on the selected identity and is disabled when nothing is selected. T005 and T006 deliberately share a name.

Tab enters or leaves the list even when it is empty. On entry the cursor goes to the selected row or the first enabled row without selecting it. Arrows and Home/End move and select; Space selects the cursor. Wheel, scrollbar track and thumb expose the rows. Selection and the cursor are identified independently of the label and position; removing, filtering, or disabling the selected row clears the application selection. The cursor recovers to the next surviving enabled row in the previous order, then the previous one.

`ListHandle` uses an application-owned `Source<std::optional<OptionValue>>` and a separately connected immediate writer. `set_items` supplies the current ordered snapshot and rejects empty or duplicate identities. The application must deliver source notifications on the UI thread and keep the source alive until the component is destroyed.

`list_page` CTest covers the application scenarios, `components` covers list admission and focus, `layout` covers row reveal and clipping, and `host_native` sends Win32 keyboard, wheel and pointer messages, including dragging the scrollbar. The existing V01 verification commands are in [the edit form README](../edit_form/README.md); the combined results are recorded in [the verification record](../edit_form/evidence/rework-verification.md).
