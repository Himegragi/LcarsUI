#pragma once
#include "x04_drivers.hpp"
#include "geometry.hpp"
#include <cmath>
#include <stdexcept>

// The same dimension and component drivers run against fake and native text measurement.
namespace presentation_matrix {
template<class Layout, class Check>
void run(const edit_form::Ids& ids, const lcars::Appearance&, Layout layout, Check check) {
    using namespace lcars;
    for (auto variant : {ThemeVariant::light(), ThemeVariant::dark()})
    for (auto density : {Density::standard, Density::compact})
    for (int size : {12, 16, 28})
    for (auto dimensions : {std::pair{960, 720}, std::pair{640, 480}})
    for (float scale : {1.f, 1.5f}) {
        edit_form::Form form(ids, true);
        auto arrange = layout(form);
        auto settings = form.ui.presentation(); settings.variant = variant; settings.density = density; settings.text_size = size;
        check(bool(form.ui.set_presentation(settings)), "MX01 settings accepted");
        int width = int(std::lround(dimensions.first * scale)), height = int(std::lround(dimensions.second * scale));
        auto entries = x04::inventory(form);
        for (const auto& entry : entries) {
            bool modal = entry.page == "modal";
            if (modal) {
                check(bool(form.handles.tabs.activate({"v03"})) && bool(form.handles.dialog_open.invoke()), "MX01 dialog opened");
            } else if (entry.page != "shared") check(bool(form.handles.tabs.activate({entry.page})), "MX01 page activated");
            auto semantic = x04::describe(entry.handle);
            if (!semantic || !semantic->focusable || !semantic->enabled) {
                if (modal) form.handles.dialog_cancel.invoke();
                continue;
            }
            check(x04::focus(entry.handle), "MX01 focus accepted: " + semantic->id);
            auto frame = arrange(width, height, scale, semantic->id);
            auto item = x04::placement(frame, entry.handle);
            bool inside = item && x04::inside_client(*item, width, height);
            bool tall = item && item->bounds.w > 0 && item->bounds.h > item->clip.h && item->clip.h > 0 &&
                intersect(item->bounds, item->clip).h == item->clip.h && intersect(item->bounds, item->clip).w == item->bounds.w;
            auto context = semantic->id + " size=" + std::to_string(size) + " scale=" + std::to_string(scale);
            check(inside || tall, "MX01 focus target revealed: " + context);
            if (inside) {
                auto hit = frame.hit(item->bounds.x + item->bounds.w / 2, item->bounds.y + item->bounds.h / 2);
                check(hit && hit->id == item->id, "MX01 hit and placement agree: " + context);
            }
            if (modal) check(bool(form.handles.dialog_cancel.invoke()), "MX01 dialog closed");
        }
        for (const auto& page : x04::pages(form)) {
            check(bool(form.handles.tabs.activate({page})), "MX01 overlap page activated");
            auto frame = arrange(width, height, scale, {});
            for (std::size_t i = 0; i < frame.items.size(); ++i) for (std::size_t j = i + 1; j < frame.items.size(); ++j) {
                auto a = intersect(frame.items[i].bounds, frame.items[i].clip);
                auto b = intersect(frame.items[j].bounds, frame.items[j].clip);
                auto overlap = intersect(a, b);
                check(overlap.w == 0 || overlap.h == 0, "MX01 overlap: " + frame.items[i].id + " / " + frame.items[j].id);
            }
        }
        check(bool(form.handles.tabs.activate({"v01"})) && bool(form.handles.region.focus()) && bool(form.handles.region.open()), "MX01 dropdown opens");
        auto popup = arrange(width, height, scale, form.ids().region);
        check(bool(test::popup_in_client(popup, {0, 0, width, height}, form.ids().region)), "MX01 popup fits and hits");
    }
}
}
