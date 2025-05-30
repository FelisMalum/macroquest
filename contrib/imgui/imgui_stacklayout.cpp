// dear imgui, v1.86 WIP
// (stack layout code)

/*

Index of this file:

// [SECTION] Commentary
// [SECTION] Header mess
// [SECTION] Stack layout: Forward declarations
// [SECTION] Stack layout: flags, enums, data structures
// [SECTION] Stack Layout: Context forward declarations
// [SECTION] Stack Layout: Internal code forward declarations
// [SECTION] Stack Layout: Context
// [SECTION] Stack Layout: Internal code
// [SECTION] Stack Layout: Internal API
// [SECTION] Stack Layout: Public API

*/

// Navigating this file:
// - In Visual Studio IDE: CTRL+comma ("Edit.NavigateTo") can follow symbols in comments, whereas CTRL+F12 ("Edit.GoToImplementation") cannot.
// - With Visual Assist installed: ALT+G ("VAssistX.GoToImplementation") can also follow symbols in comments.

//-----------------------------------------------------------------------------
// [SECTION] Commentary
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Typical call flow: (root level is generally public API):
//-----------------------------------------------------------------------------
//
// - BeginHorizontal/BeginVertical()            user begin into a layout
// - [...]                                      user emit contents
// -  | Spring()                                - separate widget groups in layout
// -  | SuspendLayout()                         - stop any layout operations
// -  | ResumeLayout()                          - resume layout operations
//-----------------------------------------------------------------------------
// - EndHorizontal/EndVertical()                user ends the layout
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// [SECTION] Header mess
//-----------------------------------------------------------------------------

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "imgui.h"
#if IMGUI_HAS_STACK_LAYOUT
#ifndef IMGUI_DISABLE
#include "imgui_internal.h"

#if defined(_MSC_VER) && _MSC_VER <= 1500 // MSVC 2008 or earlier
#include <stddef.h>     // intptr_t
#else
#include <stdint.h>     // intptr_t
#endif

//-----------------------------------------------------------------------------
// [SECTION] Stack layout: Forward declarations
//-----------------------------------------------------------------------------

// Use your programming IDE "Go to definition" facility on the names of the center columns to find the actual flags/enum lists.
typedef int ImGuiLayoutItemType;        // -> enum ImGuiLayoutItemType_     // Enum: Item or Spring


//-----------------------------------------------------------------------------
// [SECTION] Stack layout: flags, enums, data structures
//-----------------------------------------------------------------------------

enum ImGuiLayoutItemType_
{
    ImGuiLayoutItemType_Item,
    ImGuiLayoutItemType_Spring
};

// sizeof() == 48
struct ImGuiLayoutItem
{
    ImGuiLayoutItemType     Type;               // Type of an item
    ImRect                  MeasuredBounds;

    float                   SpringWeight;       // Weight of a spring
    float                   SpringSpacing;      // Spring spacing
    float                   SpringSize;         // Calculated spring size

    float                   CurrentAlign;
    float                   CurrentAlignOffset;

    unsigned int            VertexIndexBegin;
    unsigned int            VertexIndexEnd;

    ImGuiLayoutItem(ImGuiLayoutItemType type)
    {
        Type = type;
        MeasuredBounds = ImRect(0, 0, 0, 0);    // FIXME: @thedmd are you sure the default ImRect value FLT_MAX,FLT_MAX,-FLT_MAX,-FLT_MAX aren't enough here?
        SpringWeight = 1.0f;
        SpringSpacing = -1.0f;
        SpringSize = 0.0f;
        CurrentAlign = 0.0f;
        CurrentAlignOffset = 0.0f;
        VertexIndexBegin = VertexIndexEnd = (ImDrawIdx)0;
    }
};

struct ImGuiLayout
{
    ImGuiID                     Id;
    ImGuiLayoutType             Type;
    bool                        Live;
    ImVec2                      Size;               // Size passed to BeginLayout
    ImVec2                      CurrentSize;        // Bounds of layout known at the beginning the frame.
    ImVec2                      MinimumSize;        // Minimum possible size when springs are collapsed.
    ImVec2                      MeasuredSize;       // Measured size with springs expanded.

    ImVector<ImGuiLayoutItem>   Items;
    int                         CurrentItemIndex;
    int                         ParentItemIndex;
    ImGuiLayout*                Parent;
    ImGuiLayout*                FirstChild;
    ImGuiLayout*                NextSibling;
    float                       Align;              // Current item alignment.
    float                       Indent;             // Indent used to align items in vertical layout.
    ImVec2                      StartPos;           // Initial cursor position when BeginLayout is called.
    ImVec2                      StartCursorMaxPos;  // Maximum cursor position when BeginLayout is called.

    ImGuiLayout(ImGuiID id, ImGuiLayoutType type)
    {
        Id = id;
        Type = type;
        Live = false;
        Size = CurrentSize = MinimumSize = MeasuredSize = ImVec2(0, 0);
        CurrentItemIndex = 0;
        ParentItemIndex = 0;
        Parent = FirstChild = NextSibling = NULL;
        Align = -1.0f;
        Indent = 0.0f;
        StartPos = ImVec2(0, 0);
        StartCursorMaxPos = ImVec2(0, 0);
    }
};

struct ImGuiLayoutWindowState
{
    ImGuiWindow*            Window;
    ImGuiLayout*            CurrentLayout;
    ImGuiLayoutItem*        CurrentLayoutItem;
    ImVector<ImGuiLayout*>  LayoutStack;
    ImGuiStorage            Layouts;

    ImGuiLayoutWindowState()
    {
        Window            = NULL;
        CurrentLayout     = NULL;
        CurrentLayoutItem = NULL;
    }
};

struct ImGuiLayoutState
{
    ImGuiStorage    LayoutWindowStates;

    ImGuiContext*   Context;
    ImGuiID         ContextID;
    ImGuiID         NewFramePreID;
    ImGuiID         EndFramePreID;
    ImGuiID         ShutdownHookID;

    ImGuiLayoutState()
    {
        Context        = NULL;
        ContextID      = 0;
        NewFramePreID  = 0;
        EndFramePreID  = 0;
        ShutdownHookID = 0;
    }
};


//-----------------------------------------------------------------------------
// [SECTION] Stack Layout: Context forward declarations
//-----------------------------------------------------------------------------

static ImGuiStorage GStackLayoutStates;

// Context management/hooks
static ImGuiID                  GetContextID(ImGuiContext* context); // FIXME: ImGuiContext probably should have their own ID assigned
static ImGuiLayoutState*        GetCurrentLayoutState();
static ImGuiLayoutState*        GetLayoutState(ImGuiContext* context);
static ImGuiLayoutState*        CreateLayoutState(ImGuiID context_id, ImGuiContext* context);
static void                     StackLayout_NewFramePreCallback(ImGuiContext* ctx, ImGuiContextHook* hook);
static void                     StackLayout_EndFramePreCallback(ImGuiContext* ctx, ImGuiContextHook* hook);
static void                     StackLayout_ShutdownCallback(ImGuiContext* ctx, ImGuiContextHook* hook);

static ImGuiLayoutWindowState*  GetWindowLayoutState(ImGuiID window_id, ImGuiWindow* window = NULL);
static ImGuiLayoutWindowState*  GetCurrentWindowLayoutState();
static void                     WindowLayoutState_OnNewFrame(ImGuiLayoutWindowState* state);
static void                     WindowLayoutState_OnEndFrame(ImGuiLayoutWindowState* state);
static void                     WindowLayoutState_OnShutdown(ImGuiLayoutWindowState* state);


//-----------------------------------------------------------------------------
// [SECTION] Stack Layout: Internal code forward declarations
//-----------------------------------------------------------------------------

namespace ImGui
{
// Stack Layout
static ImGuiLayout*     FindLayout(ImGuiID id, ImGuiLayoutType type);
static ImGuiLayout*     CreateNewLayout(ImGuiID id, ImGuiLayoutType type, ImVec2 size);
static void             BeginLayout(ImGuiID id, ImGuiLayoutType type, ImVec2 size, float align);
static void             EndLayout(ImGuiLayoutType type);
static ImVec2           CalculateLayoutSize(ImGuiLayout& layout, bool collapse_springs);
static void             PushLayout(ImGuiLayout* layout);
static void             PopLayout(ImGuiLayout* layout);
static void             BalanceLayoutSprings(ImGuiLayout& layout);
static ImVec2           BalanceLayoutItemAlignment(ImGuiLayout& layout, ImGuiLayoutItem& item);
static void             BalanceLayoutItemsAlignment(ImGuiLayout& layout);
static bool             HasAnyNonZeroSpring(ImGuiLayout& layout);
static void             BalanceChildLayouts(ImGuiLayout& layout);
static ImGuiLayoutItem* GenerateLayoutItem(ImGuiLayout& layout, ImGuiLayoutItemType type);
static float            CalculateLayoutItemAlignmentOffset(ImGuiLayout& layout, ImGuiLayoutItem& item);
static void             TranslateLayoutItem(ImGuiLayoutItem& item, const ImVec2& offset);
static void             SignedIndent(float indent);
static void             BeginLayoutItem(ImGuiLayout& layout);
static void             EndLayoutItem(ImGuiLayout& layout);
static void             AddLayoutSpring(ImGuiLayout& layout, float weight, float spacing);
}


//-----------------------------------------------------------------------------
// [SECTION] Stack Layout: Context
//-----------------------------------------------------------------------------

static ImGuiID GetContextID(ImGuiContext* context)
{
    // Hash pointer to ImGuiContext which is unique
    return ImHashData(&context, sizeof(context));
}

static ImGuiLayoutState* GetCurrentLayoutState()
{
    return GetLayoutState(ImGui::GetCurrentContext());
}

static ImGuiLayoutState* GetLayoutState(ImGuiContext* context)
{
    if (context == NULL)
        return NULL;

    ImGuiID context_id = GetContextID(context);

    ImGuiLayoutState* state = (ImGuiLayoutState*)GStackLayoutStates.GetVoidPtr(context_id);
    if (state == NULL)
        state = CreateLayoutState(context_id, context);

    return state;
}

static ImGuiLayoutState* CreateLayoutState(ImGuiID context_id, ImGuiContext* context)
{
    ImGuiLayoutState* state = IM_NEW(ImGuiLayoutState);
    state->Context   = context;
    state->ContextID = context_id;

    ImGuiContextHook new_frame_pre_hook;
    new_frame_pre_hook.Type     = ImGuiContextHookType_NewFramePre;
    new_frame_pre_hook.UserData = state;
    new_frame_pre_hook.Callback = StackLayout_NewFramePreCallback;
    state->NewFramePreID = ImGui::AddContextHook(context, &new_frame_pre_hook);

    ImGuiContextHook end_frame_pre_hook;
    end_frame_pre_hook.Type     = ImGuiContextHookType_EndFramePre;
    end_frame_pre_hook.UserData = state;
    end_frame_pre_hook.Callback = StackLayout_EndFramePreCallback;
    state->EndFramePreID = ImGui::AddContextHook(context, &end_frame_pre_hook);

    ImGuiContextHook shutdown_hook;
    shutdown_hook.Type     = ImGuiContextHookType_Shutdown;
    shutdown_hook.UserData = state;
    shutdown_hook.Callback = StackLayout_ShutdownCallback;
    state->ShutdownHookID = ImGui::AddContextHook(context, &shutdown_hook);

    GStackLayoutStates.SetVoidPtr(context_id, state);

    return state;
}

static void StackLayout_NewFramePreCallback(ImGuiContext* ctx, ImGuiContextHook* hook)
{
    ImGuiLayoutState* layout_state = (ImGuiLayoutState*)hook->UserData;

    for (int i = 0; i < layout_state->LayoutWindowStates.Data.Size; ++i)
    {
        ImGuiLayoutWindowState* layout_window_state = (ImGuiLayoutWindowState*)layout_state->LayoutWindowStates.Data[i].val_p;

        WindowLayoutState_OnNewFrame(layout_window_state);
    }
}

static void StackLayout_EndFramePreCallback(ImGuiContext* ctx, ImGuiContextHook* hook)
{
    ImGuiLayoutState* layout_state = (ImGuiLayoutState*)hook->UserData;

    for (int i = 0; i < layout_state->LayoutWindowStates.Data.Size; ++i)
    {
        ImGuiLayoutWindowState* layout_window_state = (ImGuiLayoutWindowState*)layout_state->LayoutWindowStates.Data[i].val_p;

        WindowLayoutState_OnEndFrame(layout_window_state);
    }
}

static void StackLayout_ShutdownCallback(ImGuiContext* ctx, ImGuiContextHook* hook)
{
    ImGuiLayoutState* layout_state = (ImGuiLayoutState*)hook->UserData;

    for (int i = 0; i < layout_state->LayoutWindowStates.Data.Size; ++i)
    {
        ImGuiLayoutWindowState* layout_window_state = (ImGuiLayoutWindowState*)layout_state->LayoutWindowStates.Data[i].val_p;

        WindowLayoutState_OnShutdown(layout_window_state);

        IM_DELETE(layout_window_state);
    }

    GStackLayoutStates.SetVoidPtr(layout_state->ContextID, nullptr);

    IM_DELETE(layout_state);
}


static ImGuiLayoutWindowState* GetWindowLayoutState(ImGuiID window_id, ImGuiWindow* window)
{
    ImGuiLayoutState* layout_state = GetCurrentLayoutState();
    if (layout_state == NULL)
        return NULL;

    ImGuiLayoutWindowState* window_layout_state = (ImGuiLayoutWindowState*)layout_state->LayoutWindowStates.GetVoidPtr(window_id);
    if (window_layout_state == NULL)
    {
        if (window == NULL)
        {
            window = ImGui::FindWindowByID(window_id);
            IM_ASSERT(window && "GetWindowLayoutState called with invalid Window ID.");
        }

        window_layout_state = IM_NEW(ImGuiLayoutWindowState);
        window_layout_state->Window = window;
        layout_state->LayoutWindowStates.SetVoidPtr(window_id, window_layout_state);
    }

    return window_layout_state;

}


static ImGuiLayoutWindowState* GetCurrentWindowLayoutState()
{
    ImGuiWindow* current_window = ImGui::GetCurrentWindow();
    if (current_window == NULL)
        return NULL;

    return GetWindowLayoutState(current_window->ID, current_window);
}

static void WindowLayoutState_OnNewFrame(ImGuiLayoutWindowState* state)
{
    // Mark all layouts as dead. They may be revived in this frame.
    for (int i = 0; i < state->Layouts.Data.Size; i++)
    {
        ImGuiLayout* layout = (ImGuiLayout*)state->Layouts.Data[i].val_p;
        layout->Live = false;
    }
}

static void WindowLayoutState_OnEndFrame(ImGuiLayoutWindowState* state)
{
    // Check stacks (like ImGuiStackSizes::CompareWithCurrentState() does)
    IM_ASSERT(0 == state->LayoutStack.Size && (!state->LayoutStack.Size || state->LayoutStack.back()->Type == ImGuiLayoutType_Horizontal) && "BeginHorizontal/EndHorizontal Mismatch!");
    IM_ASSERT(0 == state->LayoutStack.Size && (!state->LayoutStack.Size || state->LayoutStack.back()->Type == ImGuiLayoutType_Vertical)   && "BeginVertical/EndVertical Mismatch!");
}

static void WindowLayoutState_OnShutdown(ImGuiLayoutWindowState* state)
{
    for (int i = 0; i < state->Layouts.Data.Size; i++)
    {
        ImGuiLayout* layout = (ImGuiLayout*)state->Layouts.Data[i].val_p;
        IM_DELETE(layout);
    }
}


//-----------------------------------------------------------------------------
// [SECTION] Stack Layout: Internal code
//-----------------------------------------------------------------------------

static ImGuiLayout* ImGui::FindLayout(ImGuiID id, ImGuiLayoutType type)
{
    IM_ASSERT(type == ImGuiLayoutType_Horizontal || type == ImGuiLayoutType_Vertical);

    ImGuiLayoutWindowState* window_state = GetCurrentWindowLayoutState();
    ImGuiLayout* layout = (ImGuiLayout*)window_state->Layouts.GetVoidPtr(id);
    if (!layout)
        return NULL;

    if (layout->Type != type)
    {
        layout->Type = type;
        layout->MinimumSize = ImVec2(0.0f, 0.0f);
        layout->Items.clear();
    }

    return layout;
}

static ImGuiLayout* ImGui::CreateNewLayout(ImGuiID id, ImGuiLayoutType type, ImVec2 size)
{
    IM_ASSERT(type == ImGuiLayoutType_Horizontal || type == ImGuiLayoutType_Vertical);

    ImGuiLayoutWindowState* window_state = GetCurrentWindowLayoutState();

    ImGuiLayout* layout = IM_NEW(ImGuiLayout)(id, type);
    layout->Size = size;

    window_state->Layouts.SetVoidPtr(id, layout);

    return layout;
}

static void ImGui::BeginLayout(ImGuiID id, ImGuiLayoutType type, ImVec2 size, float align)
{
    ImGuiWindow* window = GetCurrentWindow();

    PushID(id);

    // Find or create
    ImGuiLayout* layout = FindLayout(id, type);
    if (!layout)
        layout = CreateNewLayout(id, type, size);

    IM_ASSERT(!layout->Live && "BeginHorizontal/BeginVertical with same ID is already live in this frame. Please use PushID() to make ID's unique or rename layout.");

    layout->Live = true;

    PushLayout(layout);

    if (layout->Size.x != size.x || layout->Size.y != size.y)
        layout->Size = size;

    if (align < 0.0f)
        layout->Align = -1.0f;
    else
        layout->Align = ImClamp(align, 0.0f, 1.0f);

    // Start capture
    layout->CurrentItemIndex = 0;

    layout->CurrentSize.x = layout->Size.x > 0.0f ? layout->Size.x : layout->MinimumSize.x;
    layout->CurrentSize.y = layout->Size.y > 0.0f ? layout->Size.y : layout->MinimumSize.y;

    layout->StartPos = window->DC.CursorPos;
    layout->StartCursorMaxPos = window->DC.CursorMaxPos;

    if (type == ImGuiLayoutType_Vertical)
    {
        // Push empty item to recalculate cursor position.
        PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        Dummy(ImVec2(0.0f, 0.0f));
        PopStyleVar();

        // Indent horizontal position to match edge of the layout.
        layout->Indent = layout->StartPos.x - window->DC.CursorPos.x;
        SignedIndent(layout->Indent);
    }

    BeginLayoutItem(*layout);
}

static void ImGui::EndLayout(ImGuiLayoutType type)
{
    ImGuiLayoutWindowState* window_state = GetCurrentWindowLayoutState();
    IM_ASSERT(window_state->CurrentLayout);
    IM_ASSERT(window_state->CurrentLayout->Type == type);
    IM_UNUSED(type);

    ImGuiLayout* layout = window_state->CurrentLayout;

    EndLayoutItem(*layout);

    if (layout->CurrentItemIndex < layout->Items.Size)
        layout->Items.resize(layout->CurrentItemIndex);

    if (layout->Type == ImGuiLayoutType_Vertical)
        SignedIndent(-layout->Indent);

    PopLayout(layout);

    const bool auto_width  = layout->Size.x <= 0.0f;
    const bool auto_height = layout->Size.y <= 0.0f;

    ImVec2 new_size = layout->Size;
    if (auto_width)
        new_size.x = layout->CurrentSize.x;
    if (auto_height)
        new_size.y = layout->CurrentSize.y;

    ImVec2 new_minimum_size = CalculateLayoutSize(*layout, true);

    if (new_minimum_size.x != layout->MinimumSize.x || new_minimum_size.y != layout->MinimumSize.y)
    {
        layout->MinimumSize = new_minimum_size;

        // Shrink
        if (auto_width)
            new_size.x = new_minimum_size.x;
        if (auto_height)
            new_size.y = new_minimum_size.y;
    }

    if (!auto_width)
        new_size.x = layout->Size.x;
    if (!auto_height)
        new_size.y = layout->Size.y;

    layout->CurrentSize = new_size;

    ImVec2 measured_size = new_size;
    if ((auto_width || auto_height) && layout->Parent)
    {
        if (layout->Type == ImGuiLayoutType_Horizontal && auto_width && layout->Parent->CurrentSize.x > 0)
            layout->CurrentSize.x = layout->Parent->CurrentSize.x;
        else if (layout->Type == ImGuiLayoutType_Vertical && auto_height && layout->Parent->CurrentSize.y > 0)
            layout->CurrentSize.y = layout->Parent->CurrentSize.y;

        BalanceLayoutSprings(*layout);

        measured_size = layout->CurrentSize;
    }

    layout->CurrentSize = new_size;

    PopID();

    ImVec2 current_layout_item_max = ImVec2(0.0f, 0.0f);
    if (window_state->CurrentLayoutItem)
        current_layout_item_max = ImMax(window_state->CurrentLayoutItem->MeasuredBounds.Max, layout->StartPos + new_size);

    window_state->Window->DC.CursorPos    = layout->StartPos;
    window_state->Window->DC.CursorMaxPos = layout->StartCursorMaxPos;
    ItemSize(new_size);
    ItemAdd(ImRect(layout->StartPos, layout->StartPos + measured_size), 0);

    if (window_state->CurrentLayoutItem)
        window_state->CurrentLayoutItem->MeasuredBounds.Max = current_layout_item_max;

    if (layout->Parent == NULL)
        BalanceChildLayouts(*layout);

    //window->DrawList->AddRect(layout->StartPos, layout->StartPos + measured_size, IM_COL32(0,255,0,255));           // [DEBUG]
    //window->DrawList->AddRect(window->DC.LastItemRect.Min, window->DC.LastItemRect.Max, IM_COL32(255,255,0,255));   // [DEBUG]
}

static ImVec2 ImGui::CalculateLayoutSize(ImGuiLayout& layout, bool collapse_springs)
{
    ImVec2 bounds = ImVec2(0.0f, 0.0f);

    if (layout.Type == ImGuiLayoutType_Vertical)
    {
        for (int i = 0; i < layout.Items.Size; i++)
        {
            ImGuiLayoutItem& item = layout.Items[i];
            ImVec2 item_size = item.MeasuredBounds.GetSize();

            if (item.Type == ImGuiLayoutItemType_Item)
            {
                bounds.x  = ImMax(bounds.x, item_size.x);
                bounds.y += item_size.y;
            }
            else
            {
                bounds.y += ImFloor(item.SpringSpacing);

                if (!collapse_springs)
                    bounds.y += item.SpringSize;
            }
        }
    }
    else
    {
        for (int i = 0; i < layout.Items.Size; i++)
        {
            ImGuiLayoutItem& item = layout.Items[i];
            ImVec2 item_size = item.MeasuredBounds.GetSize();

            if (item.Type == ImGuiLayoutItemType_Item)
            {
                bounds.x += item_size.x;
                bounds.y  = ImMax(bounds.y, item_size.y);
            }
            else
            {
                bounds.x += ImFloor(item.SpringSpacing);

                if (!collapse_springs)
                    bounds.x += item.SpringSize;
            }
        }
    }

    return bounds;
}

static void ImGui::PushLayout(ImGuiLayout* layout)
{
    ImGuiLayoutWindowState* window_state = GetCurrentWindowLayoutState();

    if (layout)
    {
        layout->Parent = window_state->CurrentLayout;
        if (layout->Parent != NULL)
            layout->ParentItemIndex = layout->Parent->CurrentItemIndex;
        if (window_state->CurrentLayout)
        {
            layout->NextSibling = window_state->CurrentLayout->FirstChild;
            layout->FirstChild  = NULL;
            window_state->CurrentLayout->FirstChild = layout;
        }
        else
        {
            layout->NextSibling = NULL;
            layout->FirstChild  = NULL;
        }
    }

    window_state->LayoutStack.push_back(layout);
    window_state->CurrentLayout = layout;
    window_state->CurrentLayoutItem = NULL;
}

static void ImGui::PopLayout(ImGuiLayout* layout)
{
    ImGuiLayoutWindowState* window_state = GetCurrentWindowLayoutState();

    IM_ASSERT(!window_state->LayoutStack.empty());
    IM_ASSERT(window_state->LayoutStack.back() == layout);
    IM_UNUSED(layout);

    window_state->LayoutStack.pop_back();

    if (!window_state->LayoutStack.empty())
    {
        window_state->CurrentLayout = window_state->LayoutStack.back();
        window_state->CurrentLayoutItem = &window_state->CurrentLayout->Items[window_state->CurrentLayout->CurrentItemIndex];
    }
    else
    {
        window_state->CurrentLayout = NULL;
        window_state->CurrentLayoutItem = NULL;
    }
}

static void ImGui::BalanceLayoutSprings(ImGuiLayout& layout)
{
    // Accumulate amount of occupied space and springs weights
    float total_spring_weight = 0.0f;

    int last_spring_item_index = -1;
    for (int i = 0; i < layout.Items.Size; i++)
    {
        ImGuiLayoutItem& item = layout.Items[i];
        if (item.Type == ImGuiLayoutItemType_Spring)
        {
            total_spring_weight += item.SpringWeight;
            last_spring_item_index = i;
        }
    }

    // Determine occupied space and available space depending on layout type
    const bool  is_horizontal   = (layout.Type == ImGuiLayoutType_Horizontal);
    const bool  is_auto_sized   = ((is_horizontal ? layout.Size.x : layout.Size.y) <= 0.0f) && (layout.Parent == NULL);
    const float occupied_space  = is_horizontal ? layout.MinimumSize.x : layout.MinimumSize.y;
    const float available_space = is_auto_sized ? occupied_space : (is_horizontal ? layout.CurrentSize.x : layout.CurrentSize.y);
    const float free_space      = ImMax(available_space - occupied_space, 0.0f);

    float span_start     = 0.0f;
    float current_weight = 0.0f;
    for (int i = 0; i < layout.Items.Size; i++)
    {
        ImGuiLayoutItem& item = layout.Items[i];
        if (item.Type != ImGuiLayoutItemType_Spring)
            continue;

        float last_spring_size = item.SpringSize;

        if (free_space > 0.0f && total_spring_weight > 0.0f)
        {
            float next_weight = current_weight + item.SpringWeight;
            float span_end    = ImFloor((i == last_spring_item_index) ? free_space : (free_space * next_weight / total_spring_weight));
            float spring_size = span_end - span_start;
            item.SpringSize   = spring_size;
            span_start        = span_end;
            current_weight    = next_weight;
        }
        else
        {
            item.SpringSize = 0.0f;
        }

        // If spring changed its size, fix positioning of following items to avoid one frame visual bugs.
        if (last_spring_size != item.SpringSize)
        {
            float difference = item.SpringSize - last_spring_size;

            ImVec2 offset = is_horizontal ? ImVec2(difference, 0.0f) : ImVec2(0.0f, difference);

            item.MeasuredBounds.Max += offset;

            for (int j = i + 1; j < layout.Items.Size; j++)
            {
                ImGuiLayoutItem& translated_item = layout.Items[j];

                TranslateLayoutItem(translated_item, offset);

                translated_item.MeasuredBounds.Min += offset;
                translated_item.MeasuredBounds.Max += offset;
            }
        }
    }
}

static ImVec2 ImGui::BalanceLayoutItemAlignment(ImGuiLayout& layout, ImGuiLayoutItem& item)
{
    // Fixup item alignment if necessary.
    ImVec2 position_correction = ImVec2(0.0f, 0.0f);
    if (item.CurrentAlign > 0.0f)
    {
        float item_align_offset = CalculateLayoutItemAlignmentOffset(layout, item);
        if (item.CurrentAlignOffset != item_align_offset)
        {
            float offset = item_align_offset - item.CurrentAlignOffset;

            if (layout.Type == ImGuiLayoutType_Horizontal)
                position_correction.y = offset;
            else
                position_correction.x = offset;

            TranslateLayoutItem(item, position_correction);

            item.CurrentAlignOffset = item_align_offset;
        }
    }

    return position_correction;
}

static void ImGui::BalanceLayoutItemsAlignment(ImGuiLayout& layout)
{
    for (int i = 0; i < layout.Items.Size; ++i)
    {
        ImGuiLayoutItem& item = layout.Items[i];
        BalanceLayoutItemAlignment(layout, item);
    }
}

static bool ImGui::HasAnyNonZeroSpring(ImGuiLayout& layout)
{
    for (int i = 0; i < layout.Items.Size; ++i)
    {
        ImGuiLayoutItem& item = layout.Items[i];
        if (item.Type != ImGuiLayoutItemType_Spring)
            continue;
        if (item.SpringWeight > 0)
            return true;
    }
    return false;
}

static void ImGui::BalanceChildLayouts(ImGuiLayout& layout)
{
    for (ImGuiLayout* child = layout.FirstChild; child != NULL; child = child->NextSibling)
    {
        //ImVec2 child_layout_size = child->CurrentSize;

        // Propagate layout size down to child layouts.
        //
        // TODO: Distribution assume inner layout is only
        //       element inside parent item and assigns
        //       all available space to it.
        //
        //       Investigate how to split space between
        //       adjacent layouts.
        //
        //       Investigate how to measure non-layout items
        //       to treat them as fixed size blocks.
        //
        if (child->Type == ImGuiLayoutType_Horizontal && child->Size.x <= 0.0f)
            child->CurrentSize.x = layout.CurrentSize.x;
        else if (child->Type == ImGuiLayoutType_Vertical && child->Size.y <= 0.0f)
            child->CurrentSize.y = layout.CurrentSize.y;

        BalanceChildLayouts(*child);

        //child->CurrentSize = child_layout_size;

        if (HasAnyNonZeroSpring(*child))
        {
            // Expand item measured bounds to make alignment correct.
            ImGuiLayoutItem& item = layout.Items[child->ParentItemIndex];

            if (child->Type == ImGuiLayoutType_Horizontal && child->Size.x <= 0.0f)
                item.MeasuredBounds.Max.x = ImMax(item.MeasuredBounds.Max.x, item.MeasuredBounds.Min.x + layout.CurrentSize.x);
            else if (child->Type == ImGuiLayoutType_Vertical && child->Size.y <= 0.0f)
                item.MeasuredBounds.Max.y = ImMax(item.MeasuredBounds.Max.y, item.MeasuredBounds.Min.y + layout.CurrentSize.y);
        }
    }

    BalanceLayoutSprings(layout);
    BalanceLayoutItemsAlignment(layout);
}

static ImGuiLayoutItem* ImGui::GenerateLayoutItem(ImGuiLayout& layout, ImGuiLayoutItemType type)
{
    ImGuiContext& g = *GImGui;
    IM_ASSERT(layout.CurrentItemIndex <= layout.Items.Size);

    if (layout.CurrentItemIndex < layout.Items.Size)
    {
        ImGuiLayoutItem& item = layout.Items[layout.CurrentItemIndex];
        if (item.Type != type)
            item = ImGuiLayoutItem(type);
    }
    else
    {
        layout.Items.push_back(ImGuiLayoutItem(type));
    }

    ImGuiLayoutWindowState* window_state = GetCurrentWindowLayoutState();
    window_state->CurrentLayoutItem = &layout.Items[layout.CurrentItemIndex];

    return &layout.Items[layout.CurrentItemIndex];
}

// Calculate how many pixels from top/left layout edge item need to be moved to match
// layout alignment.
static float ImGui::CalculateLayoutItemAlignmentOffset(ImGuiLayout& layout, ImGuiLayoutItem& item)
{
    if (item.CurrentAlign <= 0.0f)
        return 0.0f;

    ImVec2 item_size = item.MeasuredBounds.GetSize();

    float layout_extent = (layout.Type == ImGuiLayoutType_Horizontal) ? layout.CurrentSize.y : layout.CurrentSize.x;
    float item_extent   = (layout.Type == ImGuiLayoutType_Horizontal) ? item_size.y : item_size.x;

    if (item_extent <= 0/* || layout_extent <= item_extent*/)
        return 0.0f;

    float align_offset = ImFloor(item.CurrentAlign * (layout_extent - item_extent));

    return align_offset;
}

static void ImGui::TranslateLayoutItem(ImGuiLayoutItem& item, const ImVec2& offset)
{
    if ((offset.x == 0.0f && offset.y == 0.0f) || (item.VertexIndexBegin == item.VertexIndexEnd))
        return;

    //IMGUI_DEBUG_LOG("TranslateLayoutItem by %f,%f\n", offset.x, offset.y);
    ImDrawList* draw_list = GetWindowDrawList();

    ImDrawVert* begin = draw_list->VtxBuffer.Data + item.VertexIndexBegin;
    ImDrawVert* end   = draw_list->VtxBuffer.Data + item.VertexIndexEnd;

    for (ImDrawVert* vtx = begin; vtx < end; ++vtx)
    {
        vtx->pos.x += offset.x;
        vtx->pos.y += offset.y;
    }
}

static void ImGui::SignedIndent(float indent)
{
    if (indent > 0.0f)
        Indent(indent);
    else if (indent < 0.0f)
        Unindent(-indent);
}

static void ImGui::BeginLayoutItem(ImGuiLayout& layout)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiLayoutItem& item = *GenerateLayoutItem(layout, ImGuiLayoutItemType_Item);

    item.CurrentAlign = layout.Align;
    if (item.CurrentAlign < 0.0f)
        item.CurrentAlign = ImClamp(g.Style.LayoutAlign, 0.0f, 1.0f);

    // Align item according to data from previous frame.
    // If layout changes in current frame alignment will
    // be corrected in EndLayout() to it visualy coherent.
    item.CurrentAlignOffset = CalculateLayoutItemAlignmentOffset(layout, item);
    if (item.CurrentAlign > 0.0f)
    {
        if (layout.Type == ImGuiLayoutType_Horizontal)
        {
            window->DC.CursorPos.y += item.CurrentAlignOffset;
        }
        else
        {
            float new_position = window->DC.CursorPos.x + item.CurrentAlignOffset;

            // Make placement behave like in horizontal case when next
            // widget is placed at very same Y position. This indent
            // make sure for vertical layout placed widgets has same X position.
            SignedIndent(item.CurrentAlignOffset);

            window->DC.CursorPos.x = new_position;
        }
    }

    item.MeasuredBounds.Min = item.MeasuredBounds.Max = window->DC.CursorPos;
    item.VertexIndexBegin = item.VertexIndexEnd = window->DrawList->_VtxCurrentIdx;
}

static void ImGui::EndLayoutItem(ImGuiLayout& layout)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    IM_ASSERT(layout.CurrentItemIndex < layout.Items.Size);

    ImGuiLayoutItem& item = layout.Items[layout.CurrentItemIndex];

    ImDrawList* draw_list = window->DrawList;
    item.VertexIndexEnd = draw_list->_VtxCurrentIdx;

    if (item.CurrentAlign > 0.0f && layout.Type == ImGuiLayoutType_Vertical)
        SignedIndent(-item.CurrentAlignOffset);

    // Fixup item alignment in case item size changed in current frame.
    ImVec2 position_correction = BalanceLayoutItemAlignment(layout, item);

    item.MeasuredBounds.Min += position_correction;
    item.MeasuredBounds.Max += position_correction;

    if (layout.Type == ImGuiLayoutType_Horizontal)
        window->DC.CursorPos.y = layout.StartPos.y;
    else
        window->DC.CursorPos.x = layout.StartPos.x;

    layout.CurrentItemIndex++;
}

static void ImGui::AddLayoutSpring(ImGuiLayout& layout, float weight, float spacing)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiLayoutItem* previous_item = &layout.Items[layout.CurrentItemIndex];

    // Undo item padding, spring should consume all space between items.
    if (layout.Type == ImGuiLayoutType_Horizontal)
        window->DC.CursorPos.x = previous_item->MeasuredBounds.Max.x;
    else
        window->DC.CursorPos.y = previous_item->MeasuredBounds.Max.y;

    previous_item = NULL; // may be invalid after call to GenerateLayoutItem()

    EndLayoutItem(layout);

    ImGuiLayoutItem* spring_item = GenerateLayoutItem(layout, ImGuiLayoutItemType_Spring);

    spring_item->MeasuredBounds.Min = spring_item->MeasuredBounds.Max = window->DC.CursorPos;

    if (weight < 0.0f)
        weight = 0.0f;

    if (spring_item->SpringWeight != weight)
        spring_item->SpringWeight = weight;

    if (spacing < 0.0f)
    {
        ImVec2 style_spacing = g.Style.ItemSpacing;
        if (layout.Type == ImGuiLayoutType_Horizontal)
            spacing = style_spacing.x;
        else
            spacing = style_spacing.y;
    }

    if (spring_item->SpringSpacing != spacing)
        spring_item->SpringSpacing = spacing;

    if (spring_item->SpringSize > 0.0f || spacing > 0.0f)
    {
        ImVec2 spring_size, spring_spacing;
        if (layout.Type == ImGuiLayoutType_Horizontal)
        {
            spring_spacing = ImVec2(0.0f, g.Style.ItemSpacing.y);
            spring_size    = ImVec2(spacing + spring_item->SpringSize, layout.CurrentSize.y);
        }
        else
        {
            spring_spacing = ImVec2(g.Style.ItemSpacing.x, 0.0f);
            spring_size    = ImVec2(layout.CurrentSize.x, spacing + spring_item->SpringSize);
        }

        PushStyleVar(ImGuiStyleVar_ItemSpacing, ImFloor(spring_spacing));
        Dummy(ImFloor(spring_size));
        PopStyleVar();
    }

    layout.CurrentItemIndex++;

    BeginLayoutItem(layout);
}


//-----------------------------------------------------------------------------
// [SECTION] Stack Layout: Internal API
//-----------------------------------------------------------------------------

ImGuiLayoutType ImGuiInternal::GetCurrentLayoutType(ImGuiID window_id)
{
    ImGuiLayoutWindowState* state = GetWindowLayoutState(window_id);

    ImGuiLayoutType layout_type = state->Window->DC.LayoutType;
    if (state->CurrentLayout)
        layout_type = state->CurrentLayout->Type;

    return layout_type;
}

void ImGuiInternal::UpdateItemRect(ImGuiID window_id, const ImVec2& min, const ImVec2& max)
{
    ImGuiLayoutWindowState* state = GetWindowLayoutState(window_id);

    if (state->CurrentLayoutItem)
        state->CurrentLayoutItem->MeasuredBounds.Max = ImMax(state->CurrentLayoutItem->MeasuredBounds.Max, max);

    IM_UNUSED(min);
}


//-----------------------------------------------------------------------------
// [SECTION] Stack Layout: Public API
//-----------------------------------------------------------------------------

void ImGui::BeginHorizontal(const char* str_id, const ImVec2& size/* = ImVec2(0, 0)*/, float align/* = -1*/)
{
    ImGuiWindow* window = GetCurrentWindow();
    BeginLayout(window->GetID(str_id), ImGuiLayoutType_Horizontal, size, align);
}

void ImGui::BeginHorizontal(const void* ptr_id, const ImVec2& size/* = ImVec2(0, 0)*/, float align/* = -1*/)
{
    ImGuiWindow* window = GetCurrentWindow();
    BeginLayout(window->GetID(ptr_id), ImGuiLayoutType_Horizontal, size, align);
}

void ImGui::BeginHorizontal(int id, const ImVec2& size/* = ImVec2(0, 0)*/, float align/* = -1*/)
{
    ImGuiWindow* window = GetCurrentWindow();
    BeginLayout(window->GetID((void*)(intptr_t)id), ImGuiLayoutType_Horizontal, size, align);
}

void ImGui::EndHorizontal()
{
    EndLayout(ImGuiLayoutType_Horizontal);
}

void ImGui::BeginVertical(const char* str_id, const ImVec2& size/* = ImVec2(0, 0)*/, float align/* = -1*/)
{
    ImGuiWindow* window = GetCurrentWindow();
    BeginLayout(window->GetID(str_id), ImGuiLayoutType_Vertical, size, align);
}

void ImGui::BeginVertical(const void* ptr_id, const ImVec2& size/* = ImVec2(0, 0)*/, float align/* = -1*/)
{
    ImGuiWindow* window = GetCurrentWindow();
    BeginLayout(window->GetID(ptr_id), ImGuiLayoutType_Vertical, size, align);
}

void ImGui::BeginVertical(int id, const ImVec2& size/* = ImVec2(0, 0)*/, float align/* = -1*/)
{
    ImGuiWindow* window = GetCurrentWindow();
    BeginLayout(window->GetID((void*)(intptr_t)id), ImGuiLayoutType_Vertical, size, align);
}

void ImGui::EndVertical()
{
    EndLayout(ImGuiLayoutType_Vertical);
}

// Inserts spring separator in layout
//      weight <= 0     : spring will always have zero size
//      weight > 0      : power of current spring
//      spacing < 0     : use default spacing if pos_x == 0, no spacing if pos_x != 0
//      spacing >= 0    : enforce spacing amount
void ImGui::Spring(float weight/* = 1.0f*/, float spacing/* = -1.0f*/)
{
    ImGuiLayoutWindowState* window_state = GetCurrentWindowLayoutState();
    IM_ASSERT(window_state->CurrentLayout);

    AddLayoutSpring(*window_state->CurrentLayout, weight, spacing);
}

void ImGui::SuspendLayout()
{
    PushLayout(NULL);
}

void ImGui::ResumeLayout()
{
    ImGuiLayoutWindowState* window_state = GetCurrentWindowLayoutState();
    IM_ASSERT(!window_state->CurrentLayout);
    IM_ASSERT(!window_state->LayoutStack.empty());
    IM_UNUSED(window_state);
    PopLayout(NULL);
}

//-----------------------------------------------------------------------------

#endif // #ifndef IMGUI_DISABLE

#endif
