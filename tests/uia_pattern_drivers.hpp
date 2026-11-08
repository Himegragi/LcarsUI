#pragma once
#include <UIAutomation.h>
#include <iostream>

// Apply the same read-side checks to every exposed element. Applicability is
// determined solely by control type; Edit patterns belong to the text slice.
inline bool patterns(IUIAutomation* uia, IUIAutomationElement* root) {
    IUIAutomationCondition* all = nullptr; IUIAutomationElementArray* nodes = nullptr;
    bool ok = SUCCEEDED(uia->CreateTrueCondition(&all)) && SUCCEEDED(root->FindAll(TreeScope_Descendants, all, &nodes));
    int count = 0; if (nodes) nodes->get_Length(&count);
    for (int i = 0; i < count; ++i) {
        IUIAutomationElement* e = nullptr; nodes->GetElement(i, &e); if (!e) { ok = false; continue; }
        CONTROLTYPEID type = 0; bool row = SUCCEEDED(e->get_CurrentControlType(&type));
        if (type == UIA_GroupControlTypeId || type == UIA_ListControlTypeId || type == UIA_TabControlTypeId || type == UIA_ComboBoxControlTypeId) {
            IUIAutomationSelectionPattern* p = nullptr; IUIAutomationElementArray* selected = nullptr; BOOL multiple = TRUE, required = FALSE; int n = 0;
            row = row && SUCCEEDED(e->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&p))) && p &&
                SUCCEEDED(p->get_CurrentCanSelectMultiple(&multiple)) && !multiple && SUCCEEDED(p->get_CurrentIsSelectionRequired(&required)) &&
                SUCCEEDED(p->GetCurrentSelection(&selected)) && selected && SUCCEEDED(selected->get_Length(&n)) && n <= 1 && (!required || n == 1);
            if (selected) selected->Release(); if (p) p->Release();
        }
        if (type == UIA_RadioButtonControlTypeId || type == UIA_ListItemControlTypeId || type == UIA_TabItemControlTypeId) {
            IUIAutomationSelectionItemPattern* p = nullptr; IUIAutomationElement* container = nullptr; BOOL selected = FALSE;
            row = row && SUCCEEDED(e->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&p))) && p &&
                SUCCEEDED(p->get_CurrentIsSelected(&selected)) && SUCCEEDED(p->get_CurrentSelectionContainer(&container)) && container;
            if (container) container->Release(); if (p) p->Release();
        }
        if (type == UIA_SliderControlTypeId || type == UIA_ProgressBarControlTypeId) {
            IUIAutomationRangeValuePattern* p = nullptr; double value = 0, minimum = 0, maximum = 0, small_step = 0, large_step = 0; BOOL readonly = FALSE;
            row = row && SUCCEEDED(e->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&p))) && p &&
                SUCCEEDED(p->get_CurrentValue(&value)) && SUCCEEDED(p->get_CurrentMinimum(&minimum)) && SUCCEEDED(p->get_CurrentMaximum(&maximum)) &&
                SUCCEEDED(p->get_CurrentSmallChange(&small_step)) && SUCCEEDED(p->get_CurrentLargeChange(&large_step)) && SUCCEEDED(p->get_CurrentIsReadOnly(&readonly)) &&
                minimum <= value && value <= maximum && small_step > 0 && large_step > 0 && (type != UIA_ProgressBarControlTypeId || readonly);
            if (p) p->Release();
        }
        if (type == UIA_ComboBoxControlTypeId) {
            IUIAutomationExpandCollapsePattern* p = nullptr; ExpandCollapseState state{};
            row = row && SUCCEEDED(e->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&p))) && p && SUCCEEDED(p->get_CurrentExpandCollapseState(&state)) &&
                (state == ExpandCollapseState_Expanded || state == ExpandCollapseState_Collapsed);
            if (p) p->Release();
        }
        if (type == UIA_WindowControlTypeId) {
            IUIAutomationWindowPattern* p = nullptr; BOOL modal = FALSE;
            row = row && SUCCEEDED(e->GetCurrentPatternAs(UIA_WindowPatternId, IID_PPV_ARGS(&p))) && p && SUCCEEDED(p->get_CurrentIsModal(&modal)) && modal;
            if (p) p->Release();
        }
        if (!row) { BSTR id = nullptr; e->get_CurrentAutomationId(&id); std::wcout << L"Pattern check failed: " << (id ? id : L"") << L'\n'; SysFreeString(id); ok = false; }
        e->Release();
    }
    if (nodes) nodes->Release(); if (all) all->Release(); return ok;
}

