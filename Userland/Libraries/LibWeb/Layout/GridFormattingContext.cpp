/*
 * Copyright (c) 2022-2023, Martin Falisse <mfalisse@outlook.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/DOM/Node.h>
#include <LibWeb/Layout/Box.h>
#include <LibWeb/Layout/GridFormattingContext.h>

namespace Web::Layout {

GridFormattingContext::GridFormattingContext(LayoutState& state, Box const& grid_container, FormattingContext* parent)
    : FormattingContext(Type::Grid, state, grid_container, parent)
{
}

GridFormattingContext::~GridFormattingContext() = default;

CSSPixels GridFormattingContext::resolve_definite_track_size(CSS::GridSize const& grid_size, AvailableSpace const& available_space)
{
    VERIFY(grid_size.is_definite());
    switch (grid_size.type()) {
    case CSS::GridSize::Type::LengthPercentage: {
        if (!grid_size.length_percentage().is_auto()) {
            return grid_size.css_size().to_px(grid_container(), available_space.width.to_px());
        }
        break;
    }
    default:
        VERIFY_NOT_REACHED();
    }
    return 0;
}

int GridFormattingContext::get_count_of_tracks(Vector<CSS::ExplicitGridTrack> const& track_list, AvailableSpace const& available_space)
{
    auto track_count = 0;
    for (auto const& explicit_grid_track : track_list) {
        if (explicit_grid_track.is_repeat() && explicit_grid_track.repeat().is_default())
            track_count += explicit_grid_track.repeat().repeat_count() * explicit_grid_track.repeat().grid_track_size_list().track_list().size();
        else
            track_count += 1;
    }

    if (track_list.size() == 1
        && track_list.first().is_repeat()
        && (track_list.first().repeat().is_auto_fill() || track_list.first().repeat().is_auto_fit())) {
        track_count = count_of_repeated_auto_fill_or_fit_tracks(track_list, available_space);
    }

    return track_count;
}

int GridFormattingContext::count_of_repeated_auto_fill_or_fit_tracks(Vector<CSS::ExplicitGridTrack> const& track_list, AvailableSpace const& available_space)
{
    // https://www.w3.org/TR/css-grid-2/#auto-repeat
    // 7.2.3.2. Repeat-to-fill: auto-fill and auto-fit repetitions
    // On a subgridded axis, the auto-fill keyword is only valid once per <line-name-list>, and repeats
    // enough times for the name list to match the subgrid’s specified grid span (falling back to 0 if
    // the span is already fulfilled).

    // Otherwise on a standalone axis, when auto-fill is given as the repetition number
    // If the grid container has a definite size or max size in the relevant axis, then the number of
    // repetitions is the largest possible positive integer that does not cause the grid to overflow the
    // content box of its grid container

    CSSPixels sum_of_grid_track_sizes = 0;
    // (treating each track as its max track sizing function if that is definite or its minimum track sizing
    // function otherwise, flooring the max track sizing function by the min track sizing function if both
    // are definite, and taking gap into account)
    // FIXME: take gap into account
    for (auto& explicit_grid_track : track_list.first().repeat().grid_track_size_list().track_list()) {
        auto track_sizing_function = explicit_grid_track;
        if (track_sizing_function.is_minmax()) {
            if (track_sizing_function.minmax().max_grid_size().is_definite() && !track_sizing_function.minmax().min_grid_size().is_definite())
                sum_of_grid_track_sizes += resolve_definite_track_size(track_sizing_function.minmax().max_grid_size(), available_space);
            else if (track_sizing_function.minmax().min_grid_size().is_definite() && !track_sizing_function.minmax().max_grid_size().is_definite())
                sum_of_grid_track_sizes += resolve_definite_track_size(track_sizing_function.minmax().min_grid_size(), available_space);
            else if (track_sizing_function.minmax().min_grid_size().is_definite() && track_sizing_function.minmax().max_grid_size().is_definite())
                sum_of_grid_track_sizes += min(resolve_definite_track_size(track_sizing_function.minmax().min_grid_size(), available_space), resolve_definite_track_size(track_sizing_function.minmax().max_grid_size(), available_space));
        } else {
            sum_of_grid_track_sizes += min(resolve_definite_track_size(track_sizing_function.grid_size(), available_space), resolve_definite_track_size(track_sizing_function.grid_size(), available_space));
        }
    }
    return max(1, static_cast<int>((get_free_space(available_space, GridDimension::Column).to_px() / sum_of_grid_track_sizes).value()));

    // For the purpose of finding the number of auto-repeated tracks in a standalone axis, the UA must
    // floor the track size to a UA-specified value to avoid division by zero. It is suggested that this
    // floor be 1px.
}

void GridFormattingContext::place_item_with_row_and_column_position(Box const& child_box)
{
    int row_start = child_box.computed_values().grid_row_start().raw_value() - 1;
    int row_end = child_box.computed_values().grid_row_end().raw_value() - 1;
    int column_start = child_box.computed_values().grid_column_start().raw_value() - 1;
    int column_end = child_box.computed_values().grid_column_end().raw_value() - 1;

    // https://www.w3.org/TR/css-grid-2/#line-placement
    // 8.3. Line-based Placement: the grid-row-start, grid-column-start, grid-row-end, and grid-column-end properties

    // https://www.w3.org/TR/css-grid-2/#grid-placement-slot
    // First attempt to match the grid area’s edge to a named grid area: if there is a grid line whose
    // line name is <custom-ident>-start (for grid-*-start) / <custom-ident>-end (for grid-*-end),
    // contributes the first such line to the grid item’s placement.

    // Otherwise, treat this as if the integer 1 had been specified along with the <custom-ident>.

    // https://www.w3.org/TR/css-grid-2/#grid-placement-int
    // Contributes the Nth grid line to the grid item’s placement. If a negative integer is given, it
    // instead counts in reverse, starting from the end edge of the explicit grid.
    if (row_end < 0)
        row_end = m_occupation_grid.row_count() + row_end + 2;
    if (column_end < 0)
        column_end = m_occupation_grid.column_count() + column_end + 2;

    // If a name is given as a <custom-ident>, only lines with that name are counted. If not enough
    // lines with that name exist, all implicit grid lines are assumed to have that name for the purpose
    // of finding this position.

    // https://www.w3.org/TR/css-grid-2/#grid-placement-span-int
    // Contributes a grid span to the grid item’s placement such that the corresponding edge of the grid
    // item’s grid area is N lines from its opposite edge in the corresponding direction. For example,
    // grid-column-end: span 2 indicates the second grid line in the endward direction from the
    // grid-column-start line.
    int row_span = 1;
    int column_span = 1;
    if (child_box.computed_values().grid_row_start().is_position() && child_box.computed_values().grid_row_end().is_span())
        row_span = child_box.computed_values().grid_row_end().raw_value();
    if (child_box.computed_values().grid_column_start().is_position() && child_box.computed_values().grid_column_end().is_span())
        column_span = child_box.computed_values().grid_column_end().raw_value();
    if (child_box.computed_values().grid_row_end().is_position() && child_box.computed_values().grid_row_start().is_span()) {
        row_span = child_box.computed_values().grid_row_start().raw_value();
        row_start = row_end - row_span;
    }
    if (child_box.computed_values().grid_column_end().is_position() && child_box.computed_values().grid_column_start().is_span()) {
        column_span = child_box.computed_values().grid_column_start().raw_value();
        column_start = column_end - column_span;
    }

    // If a name is given as a <custom-ident>, only lines with that name are counted. If not enough
    // lines with that name exist, all implicit grid lines on the side of the explicit grid
    // corresponding to the search direction are assumed to have that name for the purpose of counting
    // this span.

    // https://drafts.csswg.org/css-grid/#grid-placement-auto
    // auto
    // The property contributes nothing to the grid item’s placement, indicating auto-placement or a
    // default span of one. (See § 8 Placing Grid Items, above.)

    // https://www.w3.org/TR/css-grid-2/#common-uses-named-lines
    // 8.1.3. Named Lines and Spans
    // Instead of counting lines by number, lines can be referenced by their line name:
    if (child_box.computed_values().grid_column_end().has_line_name()) {
        if (auto grid_area_index = find_valid_grid_area(child_box.computed_values().grid_column_end().line_name()); grid_area_index > -1)
            column_end = m_valid_grid_areas[grid_area_index].column_end;
        else if (auto line_name_index = get_line_index_by_line_name(child_box.computed_values().grid_column_end().line_name(), grid_container().computed_values().grid_template_columns()); line_name_index > -1)
            column_end = line_name_index;
        else
            column_end = 1;
        column_start = column_end - 1;
    }
    if (child_box.computed_values().grid_column_start().has_line_name()) {
        if (auto grid_area_index = find_valid_grid_area(child_box.computed_values().grid_column_end().line_name()); grid_area_index > -1)
            column_start = m_valid_grid_areas[grid_area_index].column_start;
        else if (auto line_name_index = get_line_index_by_line_name(child_box.computed_values().grid_column_start().line_name(), grid_container().computed_values().grid_template_columns()); line_name_index > -1)
            column_start = line_name_index;
        else
            column_start = 0;
    }
    if (child_box.computed_values().grid_row_end().has_line_name()) {
        if (auto grid_area_index = find_valid_grid_area(child_box.computed_values().grid_row_end().line_name()); grid_area_index > -1)
            row_end = m_valid_grid_areas[grid_area_index].row_end;
        else if (auto line_name_index = get_line_index_by_line_name(child_box.computed_values().grid_row_end().line_name(), grid_container().computed_values().grid_template_rows()); line_name_index > -1)
            row_end = line_name_index;
        else
            row_end = 1;
        row_start = row_end - 1;
    }
    if (child_box.computed_values().grid_row_start().has_line_name()) {
        if (auto grid_area_index = find_valid_grid_area(child_box.computed_values().grid_row_end().line_name()); grid_area_index > -1)
            row_start = m_valid_grid_areas[grid_area_index].row_start;
        else if (auto line_name_index = get_line_index_by_line_name(child_box.computed_values().grid_row_start().line_name(), grid_container().computed_values().grid_template_rows()); line_name_index > -1)
            row_start = line_name_index;
        else
            row_start = 0;
    }

    // If there are multiple lines of the same name, they effectively establish a named set of grid
    // lines, which can be exclusively indexed by filtering the placement by name:

    // https://drafts.csswg.org/css-grid/#grid-placement-errors
    // 8.3.1. Grid Placement Conflict Handling
    // If the placement for a grid item contains two lines, and the start line is further end-ward than
    // the end line, swap the two lines. If the start line is equal to the end line, remove the end
    // line.
    if (child_box.computed_values().grid_row_start().is_position() && child_box.computed_values().grid_row_end().is_position()) {
        if (row_start > row_end)
            swap(row_start, row_end);
        if (row_start != row_end)
            row_span = row_end - row_start;
    }
    if (child_box.computed_values().grid_column_start().is_position() && child_box.computed_values().grid_column_end().is_position()) {
        if (column_start > column_end)
            swap(column_start, column_end);
        if (column_start != column_end)
            column_span = column_end - column_start;
    }

    // If the placement contains two spans, remove the one contributed by the end grid-placement
    // property.
    if (child_box.computed_values().grid_row_start().is_span() && child_box.computed_values().grid_row_end().is_span())
        row_span = child_box.computed_values().grid_row_start().raw_value();
    if (child_box.computed_values().grid_column_start().is_span() && child_box.computed_values().grid_column_end().is_span())
        column_span = child_box.computed_values().grid_column_start().raw_value();

    // FIXME: If the placement contains only a span for a named line, replace it with a span of 1.

    m_grid_items.append(GridItem(child_box, row_start, row_span, column_start, column_span));

    m_occupation_grid.maybe_add_row(row_start + 1);
    m_occupation_grid.maybe_add_column(column_start + 1);
    m_occupation_grid.set_occupied(column_start, column_start + column_span, row_start, row_start + row_span);
}

void GridFormattingContext::place_item_with_row_position(Box const& child_box)
{
    int row_start = child_box.computed_values().grid_row_start().raw_value() - 1;
    int row_end = child_box.computed_values().grid_row_end().raw_value() - 1;

    // https://www.w3.org/TR/css-grid-2/#line-placement
    // 8.3. Line-based Placement: the grid-row-start, grid-column-start, grid-row-end, and grid-column-end properties

    // https://www.w3.org/TR/css-grid-2/#grid-placement-slot
    // First attempt to match the grid area’s edge to a named grid area: if there is a grid line whose
    // line name is <custom-ident>-start (for grid-*-start) / <custom-ident>-end (for grid-*-end),
    // contributes the first such line to the grid item’s placement.

    // Otherwise, treat this as if the integer 1 had been specified along with the <custom-ident>.

    // https://www.w3.org/TR/css-grid-2/#grid-placement-int
    // Contributes the Nth grid line to the grid item’s placement. If a negative integer is given, it
    // instead counts in reverse, starting from the end edge of the explicit grid.
    if (row_end < 0)
        row_end = m_occupation_grid.row_count() + row_end + 2;

    // If a name is given as a <custom-ident>, only lines with that name are counted. If not enough
    // lines with that name exist, all implicit grid lines are assumed to have that name for the purpose
    // of finding this position.

    // https://www.w3.org/TR/css-grid-2/#grid-placement-span-int
    // Contributes a grid span to the grid item’s placement such that the corresponding edge of the grid
    // item’s grid area is N lines from its opposite edge in the corresponding direction. For example,
    // grid-column-end: span 2 indicates the second grid line in the endward direction from the
    // grid-column-start line.
    int row_span = 1;
    if (child_box.computed_values().grid_row_start().is_position() && child_box.computed_values().grid_row_end().is_span())
        row_span = child_box.computed_values().grid_row_end().raw_value();
    if (child_box.computed_values().grid_row_end().is_position() && child_box.computed_values().grid_row_start().is_span()) {
        row_span = child_box.computed_values().grid_row_start().raw_value();
        row_start = row_end - row_span;
        // FIXME: Remove me once have implemented spans overflowing into negative indexes, e.g., grid-row: span 2 / 1
        if (row_start < 0)
            row_start = 0;
    }

    // If a name is given as a <custom-ident>, only lines with that name are counted. If not enough
    // lines with that name exist, all implicit grid lines on the side of the explicit grid
    // corresponding to the search direction are assumed to have that name for the purpose of counting
    // this span.

    // https://drafts.csswg.org/css-grid/#grid-placement-auto
    // auto
    // The property contributes nothing to the grid item’s placement, indicating auto-placement or a
    // default span of one. (See § 8 Placing Grid Items, above.)

    // https://www.w3.org/TR/css-grid-2/#common-uses-named-lines
    // 8.1.3. Named Lines and Spans
    // Instead of counting lines by number, lines can be referenced by their line name:
    if (child_box.computed_values().grid_row_end().has_line_name()) {
        if (auto grid_area_index = find_valid_grid_area(child_box.computed_values().grid_row_end().line_name()); grid_area_index > -1)
            row_end = m_valid_grid_areas[grid_area_index].row_end;
        else if (auto line_name_index = get_line_index_by_line_name(child_box.computed_values().grid_row_end().line_name(), grid_container().computed_values().grid_template_rows()); line_name_index > -1)
            row_end = line_name_index;
        else
            row_end = 1;
        row_start = row_end - 1;
    }
    if (child_box.computed_values().grid_row_start().has_line_name()) {
        if (auto grid_area_index = find_valid_grid_area(child_box.computed_values().grid_row_end().line_name()); grid_area_index > -1)
            row_start = m_valid_grid_areas[grid_area_index].row_start;
        else if (auto line_name_index = get_line_index_by_line_name(child_box.computed_values().grid_row_start().line_name(), grid_container().computed_values().grid_template_rows()); line_name_index > -1)
            row_start = line_name_index;
        else
            row_start = 0;
    }

    // If there are multiple lines of the same name, they effectively establish a named set of grid
    // lines, which can be exclusively indexed by filtering the placement by name:

    // https://drafts.csswg.org/css-grid/#grid-placement-errors
    // 8.3.1. Grid Placement Conflict Handling
    // If the placement for a grid item contains two lines, and the start line is further end-ward than
    // the end line, swap the two lines. If the start line is equal to the end line, remove the end
    // line.
    if (child_box.computed_values().grid_row_start().is_position() && child_box.computed_values().grid_row_end().is_position()) {
        if (row_start > row_end)
            swap(row_start, row_end);
        if (row_start != row_end)
            row_span = row_end - row_start;
    }
    // FIXME: Have yet to find the spec for this.
    if (!child_box.computed_values().grid_row_start().is_position() && child_box.computed_values().grid_row_end().is_position() && row_end == 0)
        row_start = 0;

    // If the placement contains two spans, remove the one contributed by the end grid-placement
    // property.
    if (child_box.computed_values().grid_row_start().is_span() && child_box.computed_values().grid_row_end().is_span())
        row_span = child_box.computed_values().grid_row_start().raw_value();

    // FIXME: If the placement contains only a span for a named line, replace it with a span of 1.

    m_occupation_grid.maybe_add_row(row_start + row_span);

    int column_start = 0;
    auto column_span = child_box.computed_values().grid_column_start().is_span() ? child_box.computed_values().grid_column_start().raw_value() : 1;
    // https://drafts.csswg.org/css-grid/#auto-placement-algo
    // 8.5. Grid Item Placement Algorithm
    // 3.3. If the largest column span among all the items without a definite column position is larger
    // than the width of the implicit grid, add columns to the end of the implicit grid to accommodate
    // that column span.
    m_occupation_grid.maybe_add_column(column_span);
    bool found_available_column = false;
    for (size_t column_index = column_start; column_index < m_occupation_grid.column_count(); column_index++) {
        if (!m_occupation_grid.is_occupied(column_index, row_start)) {
            found_available_column = true;
            column_start = column_index;
            break;
        }
    }
    if (!found_available_column) {
        column_start = m_occupation_grid.column_count();
        m_occupation_grid.maybe_add_column(column_start + column_span);
    }
    m_occupation_grid.set_occupied(column_start, column_start + column_span, row_start, row_start + row_span);

    m_grid_items.append(GridItem(child_box, row_start, row_span, column_start, column_span));
}

void GridFormattingContext::place_item_with_column_position(Box const& child_box, int& auto_placement_cursor_x, int& auto_placement_cursor_y)
{
    int column_start = child_box.computed_values().grid_column_start().raw_value() - 1;
    int column_end = child_box.computed_values().grid_column_end().raw_value() - 1;

    // https://www.w3.org/TR/css-grid-2/#line-placement
    // 8.3. Line-based Placement: the grid-row-start, grid-column-start, grid-row-end, and grid-column-end properties

    // https://www.w3.org/TR/css-grid-2/#grid-placement-slot
    // First attempt to match the grid area’s edge to a named grid area: if there is a grid line whose
    // line name is <custom-ident>-start (for grid-*-start) / <custom-ident>-end (for grid-*-end),
    // contributes the first such line to the grid item’s placement.

    // Otherwise, treat this as if the integer 1 had been specified along with the <custom-ident>.

    // https://www.w3.org/TR/css-grid-2/#grid-placement-int
    // Contributes the Nth grid line to the grid item’s placement. If a negative integer is given, it
    // instead counts in reverse, starting from the end edge of the explicit grid.
    if (column_end < 0)
        column_end = m_occupation_grid.column_count() + column_end + 2;

    // If a name is given as a <custom-ident>, only lines with that name are counted. If not enough
    // lines with that name exist, all implicit grid lines are assumed to have that name for the purpose
    // of finding this position.

    // https://www.w3.org/TR/css-grid-2/#grid-placement-span-int
    // Contributes a grid span to the grid item’s placement such that the corresponding edge of the grid
    // item’s grid area is N lines from its opposite edge in the corresponding direction. For example,
    // grid-column-end: span 2 indicates the second grid line in the endward direction from the
    // grid-column-start line.
    int column_span = 1;
    auto row_span = child_box.computed_values().grid_row_start().is_span() ? child_box.computed_values().grid_row_start().raw_value() : 1;
    if (child_box.computed_values().grid_column_start().is_position() && child_box.computed_values().grid_column_end().is_span())
        column_span = child_box.computed_values().grid_column_end().raw_value();
    if (child_box.computed_values().grid_column_end().is_position() && child_box.computed_values().grid_column_start().is_span()) {
        column_span = child_box.computed_values().grid_column_start().raw_value();
        column_start = column_end - column_span;
        // FIXME: Remove me once have implemented spans overflowing into negative indexes, e.g., grid-column: span 2 / 1
        if (column_start < 0)
            column_start = 0;
    }
    // FIXME: Have yet to find the spec for this.
    if (!child_box.computed_values().grid_column_start().is_position() && child_box.computed_values().grid_column_end().is_position() && column_end == 0)
        column_start = 0;

    // If a name is given as a <custom-ident>, only lines with that name are counted. If not enough
    // lines with that name exist, all implicit grid lines on the side of the explicit grid
    // corresponding to the search direction are assumed to have that name for the purpose of counting
    // this span.

    // https://drafts.csswg.org/css-grid/#grid-placement-auto
    // auto
    // The property contributes nothing to the grid item’s placement, indicating auto-placement or a
    // default span of one. (See § 8 Placing Grid Items, above.)

    // https://www.w3.org/TR/css-grid-2/#common-uses-named-lines
    // 8.1.3. Named Lines and Spans
    // Instead of counting lines by number, lines can be referenced by their line name:
    if (child_box.computed_values().grid_column_end().has_line_name()) {
        if (auto grid_area_index = find_valid_grid_area(child_box.computed_values().grid_column_end().line_name()); grid_area_index > -1)
            column_end = m_valid_grid_areas[grid_area_index].column_end;
        else if (auto line_name_index = get_line_index_by_line_name(child_box.computed_values().grid_column_end().line_name(), grid_container().computed_values().grid_template_columns()); line_name_index > -1)
            column_end = line_name_index;
        else
            column_end = 1;
        column_start = column_end - 1;
    }
    if (child_box.computed_values().grid_column_start().has_line_name()) {
        if (auto grid_area_index = find_valid_grid_area(child_box.computed_values().grid_column_end().line_name()); grid_area_index > -1)
            column_start = m_valid_grid_areas[grid_area_index].column_start;
        else if (auto line_name_index = get_line_index_by_line_name(child_box.computed_values().grid_column_start().line_name(), grid_container().computed_values().grid_template_columns()); line_name_index > -1)
            column_start = line_name_index;
        else
            column_start = 0;
    }

    // If there are multiple lines of the same name, they effectively establish a named set of grid
    // lines, which can be exclusively indexed by filtering the placement by name:

    // https://drafts.csswg.org/css-grid/#grid-placement-errors
    // 8.3.1. Grid Placement Conflict Handling
    // If the placement for a grid item contains two lines, and the start line is further end-ward than
    // the end line, swap the two lines. If the start line is equal to the end line, remove the end
    // line.
    if (child_box.computed_values().grid_column_start().is_position() && child_box.computed_values().grid_column_end().is_position()) {
        if (column_start > column_end)
            swap(column_start, column_end);
        if (column_start != column_end)
            column_span = column_end - column_start;
    }

    // If the placement contains two spans, remove the one contributed by the end grid-placement
    // property.
    if (child_box.computed_values().grid_column_start().is_span() && child_box.computed_values().grid_column_end().is_span())
        column_span = child_box.computed_values().grid_column_start().raw_value();

    // FIXME: If the placement contains only a span for a named line, replace it with a span of 1.

    // 4.1.1.1. Set the column position of the cursor to the grid item's column-start line. If this is
    // less than the previous column position of the cursor, increment the row position by 1.
    if (column_start < auto_placement_cursor_x)
        auto_placement_cursor_y++;
    auto_placement_cursor_x = column_start;

    m_occupation_grid.maybe_add_column(auto_placement_cursor_x + 1);
    m_occupation_grid.maybe_add_row(auto_placement_cursor_y + 1);

    // 4.1.1.2. Increment the cursor's row position until a value is found where the grid item does not
    // overlap any occupied grid cells (creating new rows in the implicit grid as necessary).
    while (true) {
        if (!m_occupation_grid.is_occupied(column_start, auto_placement_cursor_y)) {
            break;
        }
        auto_placement_cursor_y++;
        m_occupation_grid.maybe_add_row(auto_placement_cursor_y + row_span);
    }
    // 4.1.1.3. Set the item's row-start line to the cursor's row position, and set the item's row-end
    // line according to its span from that position.
    m_occupation_grid.set_occupied(column_start, column_start + column_span, auto_placement_cursor_y, auto_placement_cursor_y + row_span);

    m_grid_items.append(GridItem(child_box, auto_placement_cursor_y, row_span, column_start, column_span));
}

void GridFormattingContext::place_item_with_no_declared_position(Box const& child_box, int& auto_placement_cursor_x, int& auto_placement_cursor_y)
{
    // 4.1.2.1. Increment the column position of the auto-placement cursor until either this item's grid
    // area does not overlap any occupied grid cells, or the cursor's column position, plus the item's
    // column span, overflow the number of columns in the implicit grid, as determined earlier in this
    // algorithm.
    auto column_start = 0;
    auto column_span = 1;
    if (child_box.computed_values().grid_column_start().is_span())
        column_span = child_box.computed_values().grid_column_start().raw_value();
    else if (child_box.computed_values().grid_column_end().is_span())
        column_span = child_box.computed_values().grid_column_end().raw_value();
    // https://drafts.csswg.org/css-grid/#auto-placement-algo
    // 8.5. Grid Item Placement Algorithm
    // 3.3. If the largest column span among all the items without a definite column position is larger
    // than the width of the implicit grid, add columns to the end of the implicit grid to accommodate
    // that column span.
    m_occupation_grid.maybe_add_column(column_span);
    auto row_start = 0;
    auto row_span = 1;
    if (child_box.computed_values().grid_row_start().is_span())
        row_span = child_box.computed_values().grid_row_start().raw_value();
    else if (child_box.computed_values().grid_row_end().is_span())
        row_span = child_box.computed_values().grid_row_end().raw_value();
    auto found_unoccupied_area = false;
    for (size_t row_index = auto_placement_cursor_y; row_index < m_occupation_grid.row_count(); row_index++) {
        for (size_t column_index = auto_placement_cursor_x; column_index < m_occupation_grid.column_count(); column_index++) {
            if (column_span + column_index <= m_occupation_grid.column_count()) {
                auto found_all_available = true;
                for (int span_index = 0; span_index < column_span; span_index++) {
                    if (m_occupation_grid.is_occupied(column_index + span_index, row_index))
                        found_all_available = false;
                }
                if (found_all_available) {
                    found_unoccupied_area = true;
                    column_start = column_index;
                    row_start = row_index;
                    goto finish;
                }
            }
        }
        auto_placement_cursor_x = 0;
        auto_placement_cursor_y++;
    }
finish:

    // 4.1.2.2. If a non-overlapping position was found in the previous step, set the item's row-start
    // and column-start lines to the cursor's position. Otherwise, increment the auto-placement cursor's
    // row position (creating new rows in the implicit grid as necessary), set its column position to the
    // start-most column line in the implicit grid, and return to the previous step.
    if (!found_unoccupied_area) {
        row_start = m_occupation_grid.row_count();
        m_occupation_grid.maybe_add_row(m_occupation_grid.row_count() + 1);
    }

    m_occupation_grid.set_occupied(column_start, column_start + column_span, row_start, row_start + row_span);
    m_grid_items.append(GridItem(child_box, row_start, row_span, column_start, column_span));
}

void GridFormattingContext::initialize_grid_tracks_from_definition(AvailableSpace const& available_space, Vector<CSS::ExplicitGridTrack> const& tracks_definition, Vector<TemporaryTrack>& tracks)
{
    auto track_count = get_count_of_tracks(tracks_definition, available_space);
    for (auto const& track_definition : tracks_definition) {
        auto repeat_count = (track_definition.is_repeat() && track_definition.repeat().is_default()) ? track_definition.repeat().repeat_count() : 1;
        if (track_definition.is_repeat()) {
            if (track_definition.repeat().is_auto_fill() || track_definition.repeat().is_auto_fit())
                repeat_count = track_count;
        }
        for (auto _ = 0; _ < repeat_count; _++) {
            switch (track_definition.type()) {
            case CSS::ExplicitGridTrack::Type::MinMax:
                tracks.append(TemporaryTrack(track_definition.minmax().min_grid_size(), track_definition.minmax().max_grid_size()));
                break;
            case CSS::ExplicitGridTrack::Type::Repeat:
                for (auto& explicit_grid_track : track_definition.repeat().grid_track_size_list().track_list()) {
                    auto track_sizing_function = explicit_grid_track;
                    if (track_sizing_function.is_minmax())
                        tracks.append(TemporaryTrack(track_sizing_function.minmax().min_grid_size(), track_sizing_function.minmax().max_grid_size()));
                    else
                        tracks.append(TemporaryTrack(track_sizing_function.grid_size()));
                }
                break;
            case CSS::ExplicitGridTrack::Type::Default:
                tracks.append(TemporaryTrack(track_definition.grid_size()));
                break;
            default:
                VERIFY_NOT_REACHED();
            }
        }
    }
}

void GridFormattingContext::initialize_grid_tracks_for_columns_and_rows(AvailableSpace const& available_space)
{
    initialize_grid_tracks_from_definition(available_space, grid_container().computed_values().grid_template_columns().track_list(), m_grid_columns);
    initialize_grid_tracks_from_definition(available_space, grid_container().computed_values().grid_template_rows().track_list(), m_grid_rows);

    for (size_t column_index = m_grid_columns.size(); column_index < m_occupation_grid.column_count(); column_index++)
        m_grid_columns.append(TemporaryTrack());
    for (size_t row_index = m_grid_rows.size(); row_index < m_occupation_grid.row_count(); row_index++)
        m_grid_rows.append(TemporaryTrack());
}

void GridFormattingContext::initialize_gap_tracks(AvailableSpace const& available_space)
{
    // https://www.w3.org/TR/css-grid-2/#gutters
    // 11.1. Gutters: the row-gap, column-gap, and gap properties
    // For the purpose of track sizing, each gutter is treated as an extra, empty, fixed-size track of
    // the specified size, which is spanned by any grid items that span across its corresponding grid
    // line.
    if (!grid_container().computed_values().column_gap().is_auto()) {
        auto column_gap_width = grid_container().computed_values().column_gap().to_px(grid_container(), available_space.width.to_px());
        for (size_t column_index = 0; column_index < m_grid_columns.size(); column_index++) {
            m_grid_columns_and_gaps.append(m_grid_columns[column_index]);
            if (column_index != m_grid_columns.size() - 1) {
                m_column_gap_tracks.append(TemporaryTrack(column_gap_width, true));
                m_grid_columns_and_gaps.append(m_column_gap_tracks.last());
            }
        }
    } else {
        for (auto& track : m_grid_columns) {
            m_grid_columns_and_gaps.append(track);
        }
    }
    if (!grid_container().computed_values().row_gap().is_auto()) {
        auto row_gap_height = grid_container().computed_values().row_gap().to_px(grid_container(), available_space.height.to_px());
        for (size_t row_index = 0; row_index < m_grid_rows.size(); row_index++) {
            m_grid_rows_and_gaps.append(m_grid_rows[row_index]);
            if (row_index != m_grid_rows.size() - 1) {
                m_row_gap_tracks.append(TemporaryTrack(row_gap_height, true));
                m_grid_rows_and_gaps.append(m_row_gap_tracks.last());
            }
        }
    } else {
        for (auto& track : m_grid_rows) {
            m_grid_rows_and_gaps.append(track);
        }
    }
}

void GridFormattingContext::initialize_track_sizes(AvailableSpace const& available_space, GridDimension const dimension)
{
    // https://www.w3.org/TR/css-grid-2/#algo-init
    // 12.4. Initialize Track Sizes
    // Initialize each track’s base size and growth limit.

    auto& tracks_and_gaps = dimension == GridDimension::Column ? m_grid_columns_and_gaps : m_grid_rows_and_gaps;
    auto& available_size = dimension == GridDimension::Column ? available_space.width : available_space.height;

    // For each track, if the track’s min track sizing function is:
    for (auto& track : tracks_and_gaps) {
        if (track.is_gap)
            continue;

        switch (track.min_track_sizing_function.type()) {
        // - A fixed sizing function
        // Resolve to an absolute length and use that size as the track’s initial base size.
        case CSS::GridSize::Type::LengthPercentage: {
            if (!track.min_track_sizing_function.is_auto()) {
                track.base_size = track.min_track_sizing_function.css_size().to_px(grid_container(), available_size.to_px());
            }

            break;
        }
        // - An intrinsic sizing function
        // Use an initial base size of zero.
        case CSS::GridSize::Type::FlexibleLength:
        case CSS::GridSize::Type::MaxContent:
        case CSS::GridSize::Type::MinContent: {
            track.base_size = 0;
            break;
        }
        default:
            VERIFY_NOT_REACHED();
        }

        // For each track, if the track’s max track sizing function is:
        switch (track.max_track_sizing_function.type()) {
        // - A fixed sizing function
        // Resolve to an absolute length and use that size as the track’s initial growth limit.
        case CSS::GridSize::Type::LengthPercentage: {
            if (!track.max_track_sizing_function.is_auto()) {
                track.growth_limit = track.max_track_sizing_function.css_size().to_px(grid_container(), available_size.to_px());
            } else {
                track.growth_limit = INFINITY;
            }
            break;
        }
        // - A flexible sizing function
        // Use an initial growth limit of infinity.
        case CSS::GridSize::Type::FlexibleLength: {
            track.growth_limit = INFINITY;
            break;
        }
        // - An intrinsic sizing function
        // Use an initial growth limit of infinity.
        case CSS::GridSize::Type::MaxContent:
        case CSS::GridSize::Type::MinContent: {
            track.growth_limit = INFINITY;
            break;
        }
        default:
            VERIFY_NOT_REACHED();
        }

        // In all cases, if the growth limit is less than the base size, increase the growth limit to match
        // the base size.
        if (track.growth_limit < track.base_size)
            track.growth_limit = track.base_size;
    }
}

void GridFormattingContext::resolve_intrinsic_track_sizes(AvailableSpace const& available_space, GridDimension const dimension)
{
    // https://www.w3.org/TR/css-grid-2/#algo-content
    // 12.5. Resolve Intrinsic Track Sizes
    // This step resolves intrinsic track sizing functions to absolute lengths. First it resolves those
    // sizes based on items that are contained wholly within a single track. Then it gradually adds in
    // the space requirements of items that span multiple tracks, evenly distributing the extra space
    // across those tracks insofar as possible.

    auto& tracks_and_gaps = dimension == GridDimension::Column ? m_grid_columns_and_gaps : m_grid_rows_and_gaps;
    auto& available_size = dimension == GridDimension::Column ? available_space.width : available_space.height;

    // FIXME: 1. Shim baseline-aligned items so their intrinsic size contributions reflect their baseline alignment.

    // 2. Size tracks to fit non-spanning items: For each track with an intrinsic track sizing function and
    // not a flexible sizing function, consider the items in it with a span of 1:

    size_t index = 0;
    for (auto& track : tracks_and_gaps) {
        if (track.is_gap) {
            ++index;
            continue;
        }

        Vector<GridItem&> grid_items_of_track;
        for (auto& grid_item : m_grid_items) {
            if (dimension == GridDimension::Column) {
                if (grid_item.gap_adjusted_column(grid_container()) == index && grid_item.raw_column_span() == 1) {
                    grid_items_of_track.append(grid_item);

                    track.border_left = max(track.border_left, grid_item.box().computed_values().border_left().width);
                    track.border_right = max(track.border_right, grid_item.box().computed_values().border_right().width);
                }
            } else {
                if (grid_item.gap_adjusted_row(grid_container()) == index && grid_item.raw_row_span() == 1) {
                    grid_items_of_track.append(grid_item);

                    track.border_top = max(track.border_top, grid_item.box().computed_values().border_top().width);
                    track.border_bottom = max(track.border_bottom, grid_item.box().computed_values().border_bottom().width);
                }
            }
        }

        if (!track.min_track_sizing_function.is_intrinsic_track_sizing() && !track.max_track_sizing_function.is_intrinsic_track_sizing()) {
            ++index;
            continue;
        }

        switch (track.min_track_sizing_function.type()) {
        case CSS::GridSize::Type::MinContent: {
            // If the track has a min-content min track sizing function, set its base size to the maximum of the
            // items’ min-content contributions, floored at zero.
            CSSPixels base_size = 0;
            for (auto& item : grid_items_of_track) {
                base_size = max(base_size, calculate_min_content_contribution(item, dimension));
            }
            track.base_size = base_size;
        } break;
        case CSS::GridSize::Type::MaxContent: {
            // If the track has a max-content min track sizing function, set its base size to the maximum of the
            // items’ max-content contributions, floored at zero.
            CSSPixels base_size = 0;
            for (auto& item : grid_items_of_track) {
                base_size = max(base_size, calculate_max_content_contribution(item, dimension));
            }
            track.base_size = base_size;
        } break;
        case CSS::GridSize::Type::LengthPercentage: {
            if (track.min_track_sizing_function.is_auto() && available_size.is_intrinsic_sizing_constraint()) {
                // If the track has an auto min track sizing function and the grid container is being sized under a
                // min-/max-content constraint, set the track’s base size to the maximum of its items’ limited
                // min-/max-content contributions (respectively), floored at zero.
                if (available_size.is_min_content()) {
                    CSSPixels base_size = 0;
                    for (auto& item : grid_items_of_track) {
                        base_size = max(base_size, calculate_limited_min_content_contribution(item, dimension));
                    }
                    track.base_size = base_size;
                } else if (available_size.is_max_content()) {
                    CSSPixels base_size = 0;
                    for (auto& item : grid_items_of_track) {
                        base_size = max(base_size, calculate_limited_max_content_contribution(item, dimension));
                    }
                    track.base_size = base_size;
                }
            } else if (track.min_track_sizing_function.is_auto()) {
                // Otherwise, set the track’s base size to the maximum of its items’ minimum contributions, floored at zero.
                CSSPixels base_size = 0;
                for (auto& item : grid_items_of_track) {
                    base_size = max(base_size, calculate_minimum_contribution(item, dimension));
                }
                track.base_size = base_size;
            }

            break;
        }
        case CSS::GridSize::Type::FlexibleLength: {
            // do nothing
            break;
        }
        default:
            VERIFY_NOT_REACHED();
        }

        auto const& max_track_sizing_function = track.max_track_sizing_function;
        if (max_track_sizing_function.is_min_content()) {
            // If the track has a min-content max track sizing function, set its growth limit to the maximum of
            // the items’ min-content contributions.
            CSSPixels growth_limit = 0;
            for (auto& item : grid_items_of_track) {
                growth_limit = max(growth_limit, calculate_min_content_contribution(item, dimension));
            }
            track.growth_limit = growth_limit;
        } else if (max_track_sizing_function.is_max_content() || max_track_sizing_function.is_auto()) {
            // If the track has a max-content max track sizing function, set its growth limit to the maximum of
            // the items’ max-content contributions. For fit-content() maximums, furthermore clamp this growth
            // limit by the fit-content() argument.
            CSSPixels growth_limit = 0;
            for (auto& item : grid_items_of_track) {
                growth_limit = max(growth_limit, calculate_max_content_contribution(item, dimension));
            }
            track.growth_limit = growth_limit;
        }

        // In all cases, if a track’s growth limit is now less than its base size, increase the growth limit
        // to match the base size.
        if (track.growth_limit < track.base_size)
            track.growth_limit = track.base_size;

        ++index;
    }

    // https://www.w3.org/TR/css-grid-2/#auto-repeat
    // The auto-fit keyword behaves the same as auto-fill, except that after grid item placement any
    // empty repeated tracks are collapsed. An empty track is one with no in-flow grid items placed into
    // or spanning across it. (This can result in all tracks being collapsed, if they’re all empty.)
    if (dimension == GridDimension::Column // FIXME: Handle for columns
        && grid_container().computed_values().grid_template_columns().track_list().size() == 1
        && grid_container().computed_values().grid_template_columns().track_list().first().is_repeat()
        && grid_container().computed_values().grid_template_columns().track_list().first().repeat().is_auto_fit()) {
        for (size_t idx = 0; idx < m_grid_columns.size(); idx++) {
            auto column_to_check = grid_container().computed_values().column_gap().is_auto() ? idx : idx / 2;
            if (m_occupation_grid.is_occupied(column_to_check, 0))
                continue;
            if (!grid_container().computed_values().column_gap().is_auto() && idx % 2 != 0)
                continue;

            // A collapsed track is treated as having a fixed track sizing function of 0px
            m_grid_columns[idx].base_size = 0;
            m_grid_columns[idx].growth_limit = 0;

            // FIXME: And the gutters on either side of it—including any space allotted through distributed
            // alignment—collapse.
        }
    }

    // 3. Increase sizes to accommodate spanning items crossing content-sized tracks: Next, consider the
    // items with a span of 2 that do not span a track with a flexible sizing function.
    // Repeat incrementally for items with greater spans until all items have been considered.
    size_t max_item_span = 1;
    for (auto& item : m_grid_items)
        max_item_span = max(item.span(dimension), max_item_span);
    for (size_t span = 2; span <= max_item_span; span++) {
        increase_sizes_to_accommodate_spanning_items_crossing_content_sized_tracks(dimension, 2);
    }

    // 4. Increase sizes to accommodate spanning items crossing flexible tracks: Next, repeat the previous
    // step instead considering (together, rather than grouped by span size) all items that do span a
    // track with a flexible sizing function while
    increase_sizes_to_accommodate_spanning_items_crossing_flexible_tracks(dimension);

    // 5. If any track still has an infinite growth limit (because, for example, it had no items placed in
    // it or it is a flexible track), set its growth limit to its base size.
    for (auto& track : tracks_and_gaps) {
        if (track.growth_limit == INFINITY) {
            track.growth_limit = track.base_size;
        }
    }

    for (auto& track : tracks_and_gaps)
        track.has_definite_base_size = true;
}

void GridFormattingContext::distribute_extra_space_across_spanned_tracks(CSSPixels item_size_contribution, Vector<TemporaryTrack&>& spanned_tracks)
{
    for (auto& track : spanned_tracks)
        track.planned_increase = 0;

    // 1. Find the space to distribute:
    CSSPixels spanned_tracks_sizes_sum = 0;
    for (auto& track : spanned_tracks)
        spanned_tracks_sizes_sum += track.base_size;

    // Subtract the corresponding size of every spanned track from the item’s size contribution to find the item’s
    // remaining size contribution.
    auto extra_space = max(CSSPixels(0), item_size_contribution - spanned_tracks_sizes_sum);

    // 2. Distribute space up to limits:
    while (extra_space > 0) {
        auto all_frozen = all_of(spanned_tracks, [](auto const& track) { return track.frozen; });
        if (all_frozen)
            break;

        // Find the item-incurred increase for each spanned track with an affected size by: distributing the space
        // equally among such tracks, freezing a track’s item-incurred increase as its affected size + item-incurred
        // increase reaches its limit
        CSSPixels increase_per_track = extra_space / spanned_tracks.size();
        for (auto& track : spanned_tracks) {
            if (increase_per_track >= track.growth_limit) {
                track.frozen = true;
                track.item_incurred_increase = track.growth_limit;
                extra_space -= track.growth_limit;
            } else {
                track.item_incurred_increase += increase_per_track;
                extra_space -= increase_per_track;
            }
        }
    }

    // FIXME: 3. Distribute space beyond limits

    // 4. For each affected track, if the track’s item-incurred increase is larger than the track’s planned increase
    //    set the track’s planned increase to that value.
    for (auto& track : spanned_tracks) {
        if (track.item_incurred_increase > track.planned_increase)
            track.planned_increase = track.item_incurred_increase;
    }
}

void GridFormattingContext::increase_sizes_to_accommodate_spanning_items_crossing_content_sized_tracks(GridDimension const dimension, size_t span)
{
    auto& tracks = dimension == GridDimension::Column ? m_grid_columns : m_grid_rows;
    for (auto& item : m_grid_items) {
        auto const item_span = item.span(dimension);
        if (item_span != span)
            continue;

        Vector<TemporaryTrack&> spanned_tracks;
        auto item_start_track_index = item.raw_position(dimension);
        for (size_t span = 0; span < item_span; span++) {
            auto& track = tracks[item_start_track_index + span];
            spanned_tracks.append(track);
        }

        auto item_spans_tracks_with_flexible_sizing_function = any_of(spanned_tracks, [](auto& track) {
            return track.min_track_sizing_function.is_flexible_length() || track.max_track_sizing_function.is_flexible_length();
        });
        if (item_spans_tracks_with_flexible_sizing_function)
            continue;

        // 1. For intrinsic minimums: First increase the base size of tracks with an intrinsic min track sizing
        //    function by distributing extra space as needed to accommodate these items’ minimum contributions.
        Vector<TemporaryTrack&> intrinsic_minimum_spanned_tracks;
        for (auto& track : spanned_tracks) {
            if (track.min_track_sizing_function.is_intrinsic_track_sizing())
                intrinsic_minimum_spanned_tracks.append(track);
        }
        auto item_minimum_contribution = calculate_minimum_contribution(item, dimension);
        distribute_extra_space_across_spanned_tracks(item_minimum_contribution, intrinsic_minimum_spanned_tracks);

        for (auto& track : spanned_tracks) {
            track.base_size += track.planned_increase;
        }

        // 4. If at this point any track’s growth limit is now less than its base size, increase its growth limit to
        //    match its base size.
        for (auto& track : tracks) {
            if (track.growth_limit < track.base_size)
                track.growth_limit = track.base_size;
        }
    }
}

void GridFormattingContext::increase_sizes_to_accommodate_spanning_items_crossing_flexible_tracks(GridDimension const dimension)
{
    auto& tracks = dimension == GridDimension::Column ? m_grid_columns : m_grid_rows;
    for (auto& item : m_grid_items) {
        Vector<TemporaryTrack&> spanned_tracks;
        auto item_start_track_index = item.raw_position(dimension);
        size_t span = 0;
        // FIXME: out of bounds check should not be needed here and currently present only
        //        because there is some placement bug for tracks with repeat()
        while (span < item.span(dimension) && item_start_track_index + span < tracks.size()) {
            auto& track = tracks[item_start_track_index + span];
            spanned_tracks.append(track);
            span++;
        }

        auto item_spans_tracks_with_flexible_sizing_function = any_of(spanned_tracks, [](auto& track) {
            return track.min_track_sizing_function.is_flexible_length() || track.max_track_sizing_function.is_flexible_length();
        });
        if (!item_spans_tracks_with_flexible_sizing_function)
            continue;

        // 1. For intrinsic minimums: First increase the base size of tracks with an intrinsic min track sizing
        //    function by distributing extra space as needed to accommodate these items’ minimum contributions.
        Vector<TemporaryTrack&> spanned_flexible_tracks;
        for (auto& track : spanned_tracks) {
            if (track.min_track_sizing_function.is_flexible_length())
                spanned_flexible_tracks.append(track);
        }
        auto item_minimum_contribution = calculate_limited_min_content_contribution(item, dimension);
        distribute_extra_space_across_spanned_tracks(item_minimum_contribution, spanned_flexible_tracks);

        for (auto& track : spanned_tracks) {
            track.base_size += track.planned_increase;
        }

        // 4. If at this point any track’s growth limit is now less than its base size, increase its growth limit to
        //    match its base size.
        for (auto& track : tracks) {
            if (track.growth_limit < track.base_size)
                track.growth_limit = track.base_size;
        }
    }
}

void GridFormattingContext::maximize_tracks(AvailableSpace const& available_space, GridDimension const dimension)
{
    // https://www.w3.org/TR/css-grid-2/#algo-grow-tracks
    // 12.6. Maximize Tracks

    auto& tracks = dimension == GridDimension::Column ? m_grid_columns : m_grid_rows;

    auto get_free_space_px = [&]() -> CSSPixels {
        // For the purpose of this step: if sizing the grid container under a max-content constraint, the
        // free space is infinite; if sizing under a min-content constraint, the free space is zero.
        auto free_space = get_free_space(available_space, dimension);
        if (free_space.is_max_content()) {
            return INFINITY;
        } else if (free_space.is_min_content()) {
            return 0;
        } else {
            return free_space.to_px();
        }
    };

    auto free_space_px = get_free_space_px();

    // If the free space is positive, distribute it equally to the base sizes of all tracks, freezing
    // tracks as they reach their growth limits (and continuing to grow the unfrozen tracks as needed).
    while (free_space_px > 0) {
        auto free_space_to_distribute_per_track = free_space_px / tracks.size();
        for (auto& track : tracks) {
            VERIFY(track.growth_limit != INFINITY);
            track.base_size = min(track.growth_limit, track.base_size + free_space_to_distribute_per_track);
        }
        if (get_free_space_px() == free_space_px)
            break;
        free_space_px = get_free_space_px();
    }

    // FIXME: If this would cause the grid to be larger than the grid container’s inner size as limited by its
    // max-width/height, then redo this step, treating the available grid space as equal to the grid
    // container’s inner size when it’s sized to its max-width/height.
}

void GridFormattingContext::expand_flexible_tracks(AvailableSpace const& available_space, GridDimension const dimension)
{
    // https://drafts.csswg.org/css-grid/#algo-flex-tracks
    // 12.7. Expand Flexible Tracks
    // This step sizes flexible tracks using the largest value it can assign to an fr without exceeding
    // the available space.

    auto& tracks_and_gaps = dimension == GridDimension::Column ? m_grid_columns_and_gaps : m_grid_rows_and_gaps;
    auto& available_size = dimension == GridDimension::Column ? available_space.width : available_space.height;

    auto find_the_size_of_an_fr = [&]() -> CSSPixels {
        // https://www.w3.org/TR/css-grid-2/#algo-find-fr-size

        VERIFY(available_size.is_definite());

        // 1. Let leftover space be the space to fill minus the base sizes of the non-flexible grid tracks.
        auto leftover_space = available_size.to_px();
        for (auto& track : tracks_and_gaps) {
            if (!track.max_track_sizing_function.is_flexible_length()) {
                leftover_space -= track.base_size;
            }
        }

        // 2. Let flex factor sum be the sum of the flex factors of the flexible tracks.
        //    If this value is less than 1, set it to 1 instead.
        auto flex_factor_sum = 0;
        for (auto& track : tracks_and_gaps) {
            if (track.max_track_sizing_function.is_flexible_length())
                flex_factor_sum++;
        }
        if (flex_factor_sum < 1)
            flex_factor_sum = 1;

        // 3. Let the hypothetical fr size be the leftover space divided by the flex factor sum.
        auto hypothetical_fr_size = leftover_space / flex_factor_sum;

        // FIXME: 4. If the product of the hypothetical fr size and a flexible track’s flex factor is less than the track’s
        //    base size, restart this algorithm treating all such tracks as inflexible.

        // 5. Return the hypothetical fr size.
        return hypothetical_fr_size;
    };

    // First, find the grid’s used flex fraction:
    auto flex_fraction = [&]() {
        auto free_space = get_free_space(available_space, dimension);
        // If the free space is zero or if sizing the grid container under a min-content constraint:
        if (free_space.to_px() == 0 || available_size.is_min_content()) {
            // The used flex fraction is zero.
            return CSSPixels(0);
            // Otherwise, if the free space is a definite length:
        } else if (free_space.is_definite()) {
            // The used flex fraction is the result of finding the size of an fr using all of the grid tracks and a space
            // to fill of the available grid space.
            return find_the_size_of_an_fr();
        } else {
            // FIXME
            return CSSPixels(0);
        }
    }();

    // For each flexible track, if the product of the used flex fraction and the track’s flex factor is greater than
    // the track’s base size, set its base size to that product.
    for (auto& track : tracks_and_gaps) {
        if (track.max_track_sizing_function.flexible_length() * flex_fraction > track.base_size) {
            track.base_size = track.max_track_sizing_function.flexible_length() * flex_fraction;
        }
    }
}

void GridFormattingContext::stretch_auto_tracks(AvailableSpace const& available_space, GridDimension const dimension)
{
    // https://drafts.csswg.org/css-grid/#algo-stretch
    // 12.8. Stretch auto Tracks

    auto& tracks_and_gaps = dimension == GridDimension::Column ? m_grid_columns_and_gaps : m_grid_rows_and_gaps;
    auto& available_size = dimension == GridDimension::Column ? available_space.width : available_space.height;

    // When the content-distribution property of the grid container is normal or stretch in this axis,
    // this step expands tracks that have an auto max track sizing function by dividing any remaining
    // positive, definite free space equally amongst them. If the free space is indefinite, but the grid
    // container has a definite min-width/height, use that size to calculate the free space for this
    // step instead.
    CSSPixels used_space = 0;
    for (auto& track : tracks_and_gaps) {
        if (!track.max_track_sizing_function.is_auto())
            used_space += track.base_size;
    }

    CSSPixels remaining_space = available_size.is_definite() ? available_size.to_px() - used_space : 0;
    auto count_of_auto_max_sizing_tracks = 0;
    for (auto& track : tracks_and_gaps) {
        if (track.max_track_sizing_function.is_auto())
            count_of_auto_max_sizing_tracks++;
    }

    for (auto& track : tracks_and_gaps) {
        if (track.max_track_sizing_function.is_auto())
            track.base_size = max(track.base_size, remaining_space / count_of_auto_max_sizing_tracks);
    }
}

void GridFormattingContext::run_track_sizing(AvailableSpace const& available_space, GridDimension const dimension)
{
    // https://www.w3.org/TR/css-grid-2/#algo-track-sizing
    // 12.3. Track Sizing Algorithm

    // 1. Initialize Track Sizes
    initialize_track_sizes(available_space, dimension);

    // 2. Resolve Intrinsic Track Sizes
    resolve_intrinsic_track_sizes(available_space, dimension);

    // 3. Maximize Tracks
    maximize_tracks(available_space, dimension);

    // 4. Expand Flexible Tracks
    expand_flexible_tracks(available_space, dimension);

    // 5. Expand Stretched auto Tracks
    stretch_auto_tracks(available_space, dimension);

    // If calculating the layout of a grid item in this step depends on the available space in the block
    // axis, assume the available space that it would have if any row with a definite max track sizing
    // function had that size and all other rows were infinite. If both the grid container and all
    // tracks have definite sizes, also apply align-content to find the final effective size of any gaps
    // spanned by such items; otherwise ignore the effects of track alignment in this estimation.
}

void GridFormattingContext::build_valid_grid_areas()
{
    Vector<GridArea> found_grid_areas;

    auto get_index_of_found_grid_area = [&](String needle) -> int {
        for (size_t x = 0; x < found_grid_areas.size(); x++) {
            if (found_grid_areas[x].name == needle)
                return static_cast<int>(x);
        }
        return -1;
    };

    // https://www.w3.org/TR/css-grid-2/#grid-template-areas-property
    // If a named grid area spans multiple grid cells, but those cells do not form a single
    // filled-in rectangle, the declaration is invalid.
    for (size_t y = 0; y < grid_container().computed_values().grid_template_areas().size(); y++) {
        for (size_t x = 0; x < grid_container().computed_values().grid_template_areas()[y].size(); x++) {
            auto grid_area_idx = get_index_of_found_grid_area(grid_container().computed_values().grid_template_areas()[y][x]);
            if (grid_area_idx == -1) {
                found_grid_areas.append({ grid_container().computed_values().grid_template_areas()[y][x], y, y + 1, x, x + 1 });
            } else {
                auto& grid_area = found_grid_areas[grid_area_idx];
                if (grid_area.row_start == y) {
                    if (grid_area.column_end == x)
                        grid_area.column_end = grid_area.column_end + 1;
                    else
                        return;
                } else {
                    if (grid_area.row_end == y) {
                        if (grid_area.column_start != x)
                            return;
                        grid_area.row_end = grid_area.row_end + 1;
                    } else if (grid_area.row_end == y + 1) {
                        if (grid_area.column_end < x || grid_area.column_end > x + 1)
                            return;
                    } else {
                        return;
                    }
                }
            }
        }
    }

    for (auto const& checked_grid_area : found_grid_areas)
        m_valid_grid_areas.append(checked_grid_area);
}

int GridFormattingContext::find_valid_grid_area(String const& needle)
{
    for (size_t x = 0; x < m_valid_grid_areas.size(); x++) {
        if (m_valid_grid_areas[x].name == needle)
            return static_cast<int>(x);
    }
    return -1;
}

void GridFormattingContext::place_grid_items(AvailableSpace const& available_space)
{
    auto grid_template_columns = grid_container().computed_values().grid_template_columns();
    auto grid_template_rows = grid_container().computed_values().grid_template_rows();
    auto column_count = get_count_of_tracks(grid_template_columns.track_list(), available_space);
    auto row_count = get_count_of_tracks(grid_template_rows.track_list(), available_space);

    // https://drafts.csswg.org/css-grid/#overview-placement
    // 2.2. Placing Items
    // The contents of the grid container are organized into individual grid items (analogous to
    // flex items), which are then assigned to predefined areas in the grid. They can be explicitly
    // placed using coordinates through the grid-placement properties or implicitly placed into
    // empty areas using auto-placement.
    grid_container().for_each_child_of_type<Box>([&](Box& child_box) {
        if (can_skip_is_anonymous_text_run(child_box))
            return IterationDecision::Continue;
        m_boxes_to_place.append(child_box);
        return IterationDecision::Continue;
    });

    m_occupation_grid = OccupationGrid(column_count, row_count);

    build_valid_grid_areas();

    // https://drafts.csswg.org/css-grid/#auto-placement-algo
    // 8.5. Grid Item Placement Algorithm

    // FIXME: 0. Generate anonymous grid items

    // 1. Position anything that's not auto-positioned.
    for (size_t i = 0; i < m_boxes_to_place.size(); i++) {
        auto const& child_box = m_boxes_to_place[i];
        if (is_auto_positioned_row(child_box->computed_values().grid_row_start(), child_box->computed_values().grid_row_end())
            || is_auto_positioned_column(child_box->computed_values().grid_column_start(), child_box->computed_values().grid_column_end()))
            continue;
        place_item_with_row_and_column_position(child_box);
        m_boxes_to_place.remove(i);
        i--;
    }

    // 2. Process the items locked to a given row.
    // FIXME: Do "dense" packing
    for (size_t i = 0; i < m_boxes_to_place.size(); i++) {
        auto const& child_box = m_boxes_to_place[i];
        if (is_auto_positioned_row(child_box->computed_values().grid_row_start(), child_box->computed_values().grid_row_end()))
            continue;
        place_item_with_row_position(child_box);
        m_boxes_to_place.remove(i);
        i--;
    }

    // 3. Determine the columns in the implicit grid.
    // NOTE: "implicit grid" here is the same as the m_occupation_grid

    // 3.1. Start with the columns from the explicit grid.
    // NOTE: Done in step 1.

    // 3.2. Among all the items with a definite column position (explicitly positioned items, items
    // positioned in the previous step, and items not yet positioned but with a definite column) add
    // columns to the beginning and end of the implicit grid as necessary to accommodate those items.
    // NOTE: "Explicitly positioned items" and "items positioned in the previous step" done in step 1
    // and 2, respectively. Adding columns for "items not yet positioned but with a definite column"
    // will be done in step 4.

    // 4. Position the remaining grid items.
    // For each grid item that hasn't been positioned by the previous steps, in order-modified document
    // order:
    auto auto_placement_cursor_x = 0;
    auto auto_placement_cursor_y = 0;
    for (size_t i = 0; i < m_boxes_to_place.size(); i++) {
        auto const& child_box = m_boxes_to_place[i];
        // 4.1. For sparse packing:
        // FIXME: no distinction made. See #4.2

        // 4.1.1. If the item has a definite column position:
        if (!is_auto_positioned_column(child_box->computed_values().grid_column_start(), child_box->computed_values().grid_column_end()))
            place_item_with_column_position(child_box, auto_placement_cursor_x, auto_placement_cursor_y);

        // 4.1.2. If the item has an automatic grid position in both axes:
        else
            place_item_with_no_declared_position(child_box, auto_placement_cursor_x, auto_placement_cursor_y);

        m_boxes_to_place.remove(i);
        i--;

        // FIXME: 4.2. For dense packing:
    }
}

void GridFormattingContext::run(Box const& box, LayoutMode, AvailableSpace const& available_space)
{
    place_grid_items(available_space);

    initialize_grid_tracks_for_columns_and_rows(available_space);

    initialize_gap_tracks(available_space);

    run_track_sizing(available_space, GridDimension::Column);
    run_track_sizing(available_space, GridDimension::Row);

    auto layout_box = [&](int row_start, int row_end, int column_start, int column_end, Box const& child_box) -> void {
        if (column_start < 0 || row_start < 0)
            return;
        auto& child_box_state = m_state.get_mutable(child_box);
        CSSPixels x_start = 0;
        CSSPixels x_end = 0;
        CSSPixels y_start = 0;
        CSSPixels y_end = 0;
        for (int i = 0; i < column_start; i++)
            x_start += m_grid_columns_and_gaps[i].base_size;
        for (int i = 0; i < column_end; i++)
            x_end += m_grid_columns_and_gaps[i].base_size;
        for (int i = 0; i < row_start; i++)
            y_start += m_grid_rows_and_gaps[i].full_vertical_size();
        for (int i = 0; i < row_end; i++) {
            if (i >= row_start)
                y_end += m_grid_rows_and_gaps[i].base_size;
            else
                y_end += m_grid_rows_and_gaps[i].full_vertical_size();
        }

        // A grid item containing block is created by the grid area to which it belongs.
        auto containing_block_width = max(CSSPixels(0), x_end - x_start - m_grid_columns_and_gaps[column_start].border_left - m_grid_columns_and_gaps[column_start].border_right);
        auto containing_block_height = y_end - y_start;

        auto computed_width = child_box.computed_values().width();
        auto computed_height = child_box.computed_values().height();

        auto used_width = computed_width.is_auto() ? containing_block_width : computed_width.to_px(grid_container(), containing_block_width);
        auto used_height = computed_height.is_auto() ? containing_block_height : computed_height.to_px(grid_container(), containing_block_height);

        child_box_state.set_content_width(used_width);
        child_box_state.set_content_height(used_height);

        child_box_state.offset = { x_start + m_grid_columns_and_gaps[column_start].border_left, y_start + m_grid_rows_and_gaps[row_start].border_top };

        child_box_state.border_left = child_box.computed_values().border_left().width;
        child_box_state.border_right = child_box.computed_values().border_right().width;
        child_box_state.border_top = child_box.computed_values().border_top().width;
        child_box_state.border_bottom = child_box.computed_values().border_bottom().width;

        auto available_space_for_children = AvailableSpace(AvailableSize::make_definite(child_box_state.content_width()), AvailableSize::make_definite(child_box_state.content_height()));
        if (auto independent_formatting_context = layout_inside(child_box, LayoutMode::Normal, available_space_for_children))
            independent_formatting_context->parent_context_did_dimension_child_root_box();
    };

    for (auto& grid_item : m_grid_items) {
        auto resolved_row_span = box.computed_values().row_gap().is_auto() ? grid_item.raw_row_span() : grid_item.raw_row_span() * 2;
        if (!box.computed_values().row_gap().is_auto() && grid_item.gap_adjusted_row(box) == 0)
            resolved_row_span -= 1;
        if (grid_item.gap_adjusted_row(box) + resolved_row_span > m_grid_rows.size())
            resolved_row_span = m_grid_rows_and_gaps.size() - grid_item.gap_adjusted_row(box);

        auto resolved_column_span = box.computed_values().column_gap().is_auto() ? grid_item.raw_column_span() : grid_item.raw_column_span() * 2;
        if (!box.computed_values().column_gap().is_auto() && grid_item.gap_adjusted_column(box) == 0)
            resolved_column_span -= 1;
        if (grid_item.gap_adjusted_column(box) + resolved_column_span > m_grid_columns_and_gaps.size())
            resolved_column_span = m_grid_columns_and_gaps.size() - grid_item.gap_adjusted_column(box);

        layout_box(
            grid_item.gap_adjusted_row(box),
            grid_item.gap_adjusted_row(box) + resolved_row_span,
            grid_item.gap_adjusted_column(box),
            grid_item.gap_adjusted_column(box) + resolved_column_span,
            grid_item.box());
    }

    if (available_space.height.is_intrinsic_sizing_constraint() || available_space.width.is_intrinsic_sizing_constraint())
        determine_intrinsic_size_of_grid_container(available_space);

    CSSPixels total_y = 0;
    for (auto& grid_row : m_grid_rows_and_gaps)
        total_y += grid_row.full_vertical_size();
    m_automatic_content_height = total_y;
}

void GridFormattingContext::determine_intrinsic_size_of_grid_container(AvailableSpace const& available_space)
{
    // https://www.w3.org/TR/css-grid-1/#intrinsic-sizes
    // The max-content size (min-content size) of a grid container is the sum of the grid container’s track sizes
    // (including gutters) in the appropriate axis, when the grid is sized under a max-content constraint (min-content constraint).

    if (available_space.height.is_intrinsic_sizing_constraint()) {
        CSSPixels grid_container_height = 0;
        for (auto& track : m_grid_rows) {
            grid_container_height += track.full_vertical_size();
        }
        m_state.get_mutable(grid_container()).set_content_height(grid_container_height);
    }

    if (available_space.width.is_intrinsic_sizing_constraint()) {
        CSSPixels grid_container_width = 0;
        for (auto& track : m_grid_columns) {
            grid_container_width += track.full_horizontal_size();
        }
        m_state.get_mutable(grid_container()).set_content_width(grid_container_width);
    }
}

CSSPixels GridFormattingContext::automatic_content_width() const
{
    return m_state.get(grid_container()).content_width();
}

CSSPixels GridFormattingContext::automatic_content_height() const
{
    return m_automatic_content_height;
}

bool GridFormattingContext::is_auto_positioned_row(CSS::GridTrackPlacement const& grid_row_start, CSS::GridTrackPlacement const& grid_row_end) const
{
    return is_auto_positioned_track(grid_row_start, grid_row_end);
}

bool GridFormattingContext::is_auto_positioned_column(CSS::GridTrackPlacement const& grid_column_start, CSS::GridTrackPlacement const& grid_column_end) const
{
    return is_auto_positioned_track(grid_column_start, grid_column_end);
}

bool GridFormattingContext::is_auto_positioned_track(CSS::GridTrackPlacement const& grid_track_start, CSS::GridTrackPlacement const& grid_track_end) const
{
    return grid_track_start.is_auto_positioned() && grid_track_end.is_auto_positioned();
}

AvailableSize GridFormattingContext::get_free_space(AvailableSpace const& available_space, GridDimension const dimension) const
{
    // https://www.w3.org/TR/css-grid-2/#algo-terms
    // free space: Equal to the available grid space minus the sum of the base sizes of all the grid
    // tracks (including gutters), floored at zero. If available grid space is indefinite, the free
    // space is indefinite as well.
    auto& available_size = dimension == GridDimension::Column ? available_space.width : available_space.height;
    auto& tracks = dimension == GridDimension::Column ? m_grid_columns_and_gaps : m_grid_rows_and_gaps;
    if (available_size.is_definite()) {
        CSSPixels sum_base_sizes = 0;
        for (auto& track : tracks)
            sum_base_sizes += track.base_size;
        return AvailableSize::make_definite(max(CSSPixels(0), available_size.to_px() - sum_base_sizes));
    }

    return available_size;
}

int GridFormattingContext::get_line_index_by_line_name(String const& needle, CSS::GridTrackSizeList grid_track_size_list)
{
    if (grid_track_size_list.track_list().size() == 0)
        return -1;

    auto repeated_tracks_count = 0;
    for (size_t x = 0; x < grid_track_size_list.track_list().size(); x++) {
        if (grid_track_size_list.track_list()[x].is_repeat()) {
            // FIXME: Calculate amount of columns/rows if auto-fill/fit
            if (!grid_track_size_list.track_list()[x].repeat().is_default())
                return -1;
            auto repeat = grid_track_size_list.track_list()[x].repeat().grid_track_size_list();
            for (size_t y = 0; y < repeat.track_list().size(); y++) {
                for (size_t z = 0; z < repeat.line_names()[y].size(); z++) {
                    if (repeat.line_names()[y][z] == needle)
                        return x + repeated_tracks_count;
                    repeated_tracks_count++;
                }
            }
        } else {
            for (size_t y = 0; y < grid_track_size_list.line_names()[x].size(); y++) {
                if (grid_track_size_list.line_names()[x][y] == needle)
                    return x + repeated_tracks_count;
            }
        }
    }
    for (size_t y = 0; y < grid_track_size_list.line_names()[grid_track_size_list.track_list().size()].size(); y++) {
        if (grid_track_size_list.line_names()[grid_track_size_list.track_list().size()][y] == needle)
            return grid_track_size_list.track_list().size() + repeated_tracks_count;
    }
    return -1;
}

OccupationGrid::OccupationGrid(size_t column_count, size_t row_count)
{
    Vector<bool> occupation_grid_row;
    for (size_t column_index = 0; column_index < max(column_count, 1); column_index++)
        occupation_grid_row.append(false);
    for (size_t row_index = 0; row_index < max(row_count, 1); row_index++)
        m_occupation_grid.append(occupation_grid_row);
}

OccupationGrid::OccupationGrid()
{
}

void OccupationGrid::maybe_add_column(size_t needed_number_of_columns)
{
    if (needed_number_of_columns <= column_count())
        return;
    auto column_count_before_modification = column_count();
    for (auto& occupation_grid_row : m_occupation_grid)
        for (size_t idx = 0; idx < needed_number_of_columns - column_count_before_modification; idx++)
            occupation_grid_row.append(false);
}

void OccupationGrid::maybe_add_row(size_t needed_number_of_rows)
{
    if (needed_number_of_rows <= row_count())
        return;

    Vector<bool> new_occupation_grid_row;
    for (size_t idx = 0; idx < column_count(); idx++)
        new_occupation_grid_row.append(false);

    for (size_t idx = 0; idx < needed_number_of_rows - row_count(); idx++)
        m_occupation_grid.append(new_occupation_grid_row);
}

void OccupationGrid::set_occupied(size_t column_start, size_t column_end, size_t row_start, size_t row_end)
{
    for (size_t row_index = 0; row_index < row_count(); row_index++) {
        if (row_index >= row_start && row_index < row_end) {
            for (size_t column_index = 0; column_index < column_count(); column_index++) {
                if (column_index >= column_start && column_index < column_end)
                    set_occupied(column_index, row_index);
            }
        }
    }
}

void OccupationGrid::set_occupied(size_t column_index, size_t row_index)
{
    m_occupation_grid[row_index][column_index] = true;
}

bool OccupationGrid::is_occupied(size_t column_index, size_t row_index)
{
    return m_occupation_grid[row_index][column_index];
}

size_t GridItem::gap_adjusted_row(Box const& grid_box) const
{
    return grid_box.computed_values().row_gap().is_auto() ? m_row : m_row * 2;
}

size_t GridItem::gap_adjusted_column(Box const& grid_box) const
{
    return grid_box.computed_values().column_gap().is_auto() ? m_column : m_column * 2;
}

CSS::Size const& GridFormattingContext::get_item_preferred_size(GridItem const& item, GridDimension const dimension) const
{
    if (dimension == GridDimension::Column)
        return item.box().computed_values().width();
    return item.box().computed_values().height();
}

CSSPixels GridFormattingContext::calculate_min_content_size(GridItem const& item, GridDimension const dimension) const
{
    if (dimension == GridDimension::Column) {
        return calculate_min_content_width(item.box());
    } else {
        return calculate_min_content_height(item.box(), get_available_space_for_item(item).width);
    }
}

CSSPixels GridFormattingContext::calculate_max_content_size(GridItem const& item, GridDimension const dimension) const
{
    if (dimension == GridDimension::Column) {
        return calculate_max_content_width(item.box());
    } else {
        return calculate_max_content_height(item.box(), get_available_space_for_item(item).width);
    }
}

CSSPixels GridFormattingContext::containing_block_size_for_item(GridItem const& item, GridDimension const dimension) const
{
    auto const& tracks = dimension == GridDimension::Column ? m_grid_columns : m_grid_rows;
    auto const track_index = item.raw_position(dimension);
    return tracks[track_index].base_size;
}

AvailableSpace GridFormattingContext::get_available_space_for_item(GridItem const& item) const
{
    auto const& column_track = m_grid_columns[item.raw_column()];
    AvailableSize available_width = column_track.has_definite_base_size ? AvailableSize::make_definite(column_track.base_size) : AvailableSize::make_indefinite();

    auto const& row_track = m_grid_rows[item.raw_row()];
    AvailableSize available_height = row_track.has_definite_base_size ? AvailableSize::make_definite(row_track.base_size) : AvailableSize::make_indefinite();

    return AvailableSpace(available_width, available_height);
}

CSSPixels GridFormattingContext::calculate_min_content_contribution(GridItem const& item, GridDimension const dimension) const
{
    auto available_space_for_item = get_available_space_for_item(item);

    auto should_treat_preferred_size_as_auto = [&] {
        if (dimension == GridDimension::Column)
            return should_treat_width_as_auto(item.box(), available_space_for_item);
        return should_treat_height_as_auto(item.box(), available_space_for_item);
    }();

    if (should_treat_preferred_size_as_auto) {
        return calculate_min_content_size(item, dimension);
    }

    auto preferred_size = get_item_preferred_size(item, dimension);
    auto containing_block_size = containing_block_size_for_item(item, dimension);
    return preferred_size.to_px(grid_container(), containing_block_size);
}

CSSPixels GridFormattingContext::calculate_max_content_contribution(GridItem const& item, GridDimension const dimension) const
{
    auto available_space_for_item = get_available_space_for_item(item);

    auto should_treat_preferred_size_as_auto = [&] {
        if (dimension == GridDimension::Column)
            return should_treat_width_as_auto(item.box(), available_space_for_item);
        return should_treat_height_as_auto(item.box(), available_space_for_item);
    }();

    if (should_treat_preferred_size_as_auto) {
        return calculate_max_content_size(item, dimension);
    }

    auto preferred_size = get_item_preferred_size(item, dimension);
    auto containing_block_size = containing_block_size_for_item(item, dimension);
    return preferred_size.to_px(grid_container(), containing_block_size);
}

CSSPixels GridFormattingContext::calculate_limited_min_content_contribution(GridItem const& item, GridDimension const dimension) const
{
    // The limited min-content contribution of an item is its min-content contribution,
    // limited by the max track sizing function (which could be the argument to a fit-content() track
    // sizing function) if that is fixed and ultimately floored by its minimum contribution.
    // FIXME: limit by max track sizing function
    auto min_content_contribution = calculate_min_content_contribution(item, dimension);
    auto minimum_contribution = calculate_minimum_contribution(item, dimension);
    if (min_content_contribution < minimum_contribution)
        return minimum_contribution;
    return min_content_contribution;
}

CSSPixels GridFormattingContext::calculate_limited_max_content_contribution(GridItem const& item, GridDimension const dimension) const
{
    // The limited max-content contribution of an item is its max-content contribution,
    // limited by the max track sizing function (which could be the argument to a fit-content() track
    // sizing function) if that is fixed and ultimately floored by its minimum contribution.
    // FIXME: limit by max track sizing function
    auto max_content_contribution = calculate_max_content_contribution(item, dimension);
    auto minimum_contribution = calculate_minimum_contribution(item, dimension);
    if (max_content_contribution < minimum_contribution)
        return minimum_contribution;
    return max_content_contribution;
}

CSS::Size const& GridFormattingContext::get_item_minimum_size(GridItem const& item, GridDimension const dimension) const
{
    if (dimension == GridDimension::Column)
        return item.box().computed_values().min_width();
    return item.box().computed_values().min_height();
}

CSSPixels GridFormattingContext::content_size_suggestion(GridItem const& item, GridDimension const dimension) const
{
    // The content size suggestion is the min-content size in the relevant axis
    // FIXME: clamped, if it has a preferred aspect ratio, by any definite opposite-axis minimum and maximum sizes
    // converted through the aspect ratio.
    return calculate_min_content_size(item, dimension);
}

Optional<CSSPixels> GridFormattingContext::specified_size_suggestion(GridItem const& item, GridDimension const dimension) const
{
    // https://www.w3.org/TR/css-grid-1/#specified-size-suggestion
    // If the item’s preferred size in the relevant axis is definite, then the specified size suggestion is that size.
    // It is otherwise undefined.
    auto const& used_values = m_state.get(item.box());
    auto has_definite_preferred_size = dimension == GridDimension::Column ? used_values.has_definite_width() : used_values.has_definite_height();
    if (has_definite_preferred_size) {
        // FIXME: consider margins, padding and borders because it is outer size.
        auto containing_block_size = containing_block_size_for_item(item, dimension);
        return item.box().computed_values().width().to_px(item.box(), containing_block_size);
    }

    return {};
}

CSSPixels GridFormattingContext::content_based_minimum_size(GridItem const& item, GridDimension const dimension) const
{
    // https://www.w3.org/TR/css-grid-1/#content-based-minimum-size
    // The content-based minimum size for a grid item in a given dimension is its specified size suggestion if it exists
    if (auto specified_size_suggestion = this->specified_size_suggestion(item, dimension); specified_size_suggestion.has_value()) {
        return specified_size_suggestion.value();
    }
    // FIXME: otherwise its transferred size suggestion if that exists
    // else its content size suggestion
    return content_size_suggestion(item, dimension);
}

CSSPixels GridFormattingContext::automatic_minimum_size(GridItem const& item, GridDimension const dimension) const
{
    // To provide a more reasonable default minimum size for grid items, the used value of its automatic minimum size
    // in a given axis is the content-based minimum size if all of the following are true:
    // - it is not a scroll container
    // - it spans at least one track in that axis whose min track sizing function is auto
    // FIXME: - if it spans more than one track in that axis, none of those tracks are flexible
    auto const& tracks = dimension == GridDimension::Column ? m_grid_columns : m_grid_rows;
    auto item_track_index = item.raw_position(dimension);

    // FIXME: Check all tracks spanned by an item
    auto item_spans_auto_tracks = tracks[item_track_index].min_track_sizing_function.is_auto();
    if (item_spans_auto_tracks && !item.box().is_scroll_container()) {
        return content_based_minimum_size(item, dimension);
    }

    // Otherwise, the automatic minimum size is zero, as usual.
    return 0;
}

CSSPixels GridFormattingContext::calculate_minimum_contribution(GridItem const& item, GridDimension const dimension) const
{
    // The minimum contribution of an item is the smallest outer size it can have.
    // Specifically, if the item’s computed preferred size behaves as auto or depends on the size of its
    // containing block in the relevant axis, its minimum contribution is the outer size that would
    // result from assuming the item’s used minimum size as its preferred size; else the item’s minimum
    // contribution is its min-content contribution. Because the minimum contribution often depends on
    // the size of the item’s content, it is considered a type of intrinsic size contribution.

    auto preferred_size = get_item_preferred_size(item, dimension);
    auto should_treat_preferred_size_as_auto = [&] {
        if (dimension == GridDimension::Column)
            return should_treat_width_as_auto(item.box(), get_available_space_for_item(item));
        return should_treat_height_as_auto(item.box(), get_available_space_for_item(item));
    }();

    if (should_treat_preferred_size_as_auto) {
        auto minimum_size = get_item_minimum_size(item, dimension);
        if (minimum_size.is_auto())
            return automatic_minimum_size(item, dimension);
        auto containing_block_size = containing_block_size_for_item(item, dimension);
        return minimum_size.to_px(grid_container(), containing_block_size);
    }

    return calculate_min_content_contribution(item, dimension);
}

}
