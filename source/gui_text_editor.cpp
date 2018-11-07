/**
 * The following implementation is heavily based on https://github.com/BalazsJako/ImGuiColorTextEdit.
 * As such this code file is licensed differently, following the requirements of the original license:
 *
 * Copyright (C) 2017 BalazsJako
 * Copyright (C) 2018 Patrick Mours
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "gui_text_editor.hpp"
#include "effect_lexer.hpp"
#include <imgui.h>

enum color_palette
{
	color_default,
	color_keyword,
	color_number_literal,
	color_string_literal,
	color_char_literal,
	color_punctuation,
	color_preprocessor,
	color_identifier,
	color_known_identifier,
	color_preprocessor_identifier,
	color_comment,
	color_multiline_comment,
	color_background,
	color_cursor,
	color_selection,
	color_error_marker,
	color_line_number,
	color_current_line_fill,
	color_current_line_fill_inactive,
	color_current_line_edge,

	color_palette_max
};

code_editor_widget::code_editor_widget()
{
	_palette = std::array<ImU32, color_palette_max> {
		0xffffffff,	// color_default
		0xffd69c56,	// color_keyword	
		0xff00ff00,	// color_number_literal
		0xff7070e0,	// color_string_literal
		0xff70a0e0, // color_char_literal
		0xffffffff, // color_punctuation
		0xff409090,	// color_preprocessor
		0xffaaaaaa, // color_identifier
		0xff9bc64d, // color_known_identifier
		0xffc040a0, // color_preprocessor_identifier
		0xff206020, // color_comment
		0xff406020, // color_multiline_comment
		0xff101010, // color_background
		0xffe0e0e0, // color_cursor
		0x80a06020, // color_selection
		0x800020ff, // color_error_marker
		0xff707000, // color_line_number
		0x40000000, // color_current_line_fill
		0x40808080, // color_current_line_fill_inactive
		0x40a0a0a0, // color_current_line_edge
	};

	_lines.emplace_back();
}

void code_editor_widget::render(const char *title, bool border)
{
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(_palette[int8_t(color_palette::color_background)]));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

	ImGui::BeginChild(title, ImVec2(), border, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar | ImGuiWindowFlags_NoMove);
	ImGui::PushAllowKeyboardFocus(true);

	char buf[128] = "", *buf_end = buf;

	// Deduce text start offset by evaluating maximum number of lines plus two spaces as text width
	snprintf(buf, 16, " %d ", static_cast<int>(_lines.size()));
	const float text_start = ImGui::CalcTextSize(buf).x + _left_margin;
	// Compute char advance offset regarding to scaled font size
	const ImVec2 char_advance = ImVec2(ImGui::CalcTextSize("#").x, ImGui::GetTextLineHeightWithSpacing() * _line_spacing);

	const auto mouse_to_text_pos = [&](const ImVec2 &mouse_pos) {
		const ImVec2 origin = ImGui::GetCursorScreenPos();

		text_pos res;
		res.line = std::max(0, (int)floor((mouse_pos.y - origin.y) / char_advance.y));
		res.line = std::min(res.line, (int)_lines.size() - 1);

		float column_width = 0.0f;
		std::string cumulated_string = "";
		float cumulated_string_width[2] = { 0.0f, 0.0f }; // [0] is the lastest, [1] is the previous. I use that trick to check where cursor is exactly (important for tabs)

		const auto &line = _lines[res.line];

		// First we find the hovered column coord
		while (text_start + cumulated_string_width[0] < (mouse_pos.x - origin.x) && res.column < int(line.size()))
		{
			cumulated_string_width[1] = cumulated_string_width[0];
			cumulated_string += line[res.column].c;
			cumulated_string_width[0] = ImGui::CalcTextSize(cumulated_string.c_str()).x;
			column_width = (cumulated_string_width[0] - cumulated_string_width[1]);
			res.column++;
		}

		// Then we reduce by 1 column coord if cursor is on the left side of the hovered column
		if (text_start + cumulated_string_width[0] - column_width / 2.0f > (mouse_pos.x - origin.x))
			res.column = std::max(0, --res.column);

		return res;
	};
	const auto calc_text_distance_to_line_begin = [this](const text_pos &from)
	{
		auto& line = _lines[from.line];
		float distance = 0.0f;
		auto fontScale = ImGui::GetFontSize() / ImGui::GetFont()->FontSize;
		float spaceSize = ImGui::CalcTextSize(" ").x + 1.0f * fontScale;
		for (size_t it = 0u; it < line.size() && it < (unsigned)from.column; ++it)
		{
			if (line[it].c == '\t')
			{
				distance = (1.0f * fontScale + std::floor((1.0f + distance)) / (float(_tab_size) * spaceSize)) * (float(_tab_size) * spaceSize);
			}
			else
			{
				char tempCString[2];
				tempCString[0] = line[it].c;
				tempCString[1] = '\0';
				distance += ImGui::CalcTextSize(tempCString).x + 1.0f * fontScale;
			}
		}

		return distance;
	};

	ImGuiIO &io = ImGui::GetIO();

	_cursor_anim += io.DeltaTime;

	const bool ctrl = io.KeyCtrl, shift = io.KeyShift, alt = io.KeyAlt;

	// Handle keyboard input
	if (ImGui::IsWindowFocused())
	{
		if (ImGui::IsWindowHovered())
			ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

		io.WantTextInput = true;
		io.WantCaptureKeyboard = true;

		     if (ctrl && !shift && !alt && ImGui::IsKeyPressed('Z'))
			{ /* undo() */ }
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed('Y'))
			{ /* redo(); */ }
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_UpArrow)))
			move_up(1, shift);
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_DownArrow)))
			move_down(1, shift);
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_LeftArrow)))
			move_left(1, shift, ctrl);
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_RightArrow)))
			move_right(1, shift, ctrl);
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageUp)))
			move_up((int)floor((ImGui::GetWindowHeight() - 20.0f) / char_advance.y) - 4, shift);
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageDown)))
			move_down((int)floor((ImGui::GetWindowHeight() - 20.0f) / char_advance.y) - 4, shift);
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Home)))
			ctrl ? move_top(shift) : move_home(shift);
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_End)))
			ctrl ? move_bottom(shift) : move_end(shift);
		else if (!ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete)))
			delete_next();
		else if (!ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Backspace)))
			delete_previous();
		else if (!alt && ImGui::IsKeyPressed(45)) // Insert
			ctrl ? clipboard_copy() : shift ? clipboard_paste() : _overwrite ^= true;
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed('C'))
			clipboard_copy();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed('V'))
			clipboard_paste();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed('X'))
			clipboard_cut();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_A)))
			select_all();
		else if (!ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter)))
			insert_character('\n', false);
		else
			for (size_t i = 0; i < _countof(io.InputCharacters); i++)
				if (const auto c = static_cast<unsigned char>(io.InputCharacters[i]); c != 0 && (isprint(c) || isspace(c)))
					insert_character(static_cast<char>(c), shift);
	}

	// Handle mouse input
	if (ImGui::IsWindowHovered() && !shift && !alt)
	{
		const bool is_clicked = ImGui::IsMouseClicked(0);
		const bool is_double_click = ImGui::IsMouseDoubleClicked(0);
		const bool is_triple_click = is_clicked && !is_double_click && ImGui::GetTime() - _last_click_time < io.MouseDoubleClickTime;

		if (is_triple_click)
		{
			if (!ctrl)
			{
				_cursor_pos = mouse_to_text_pos(ImGui::GetMousePos());
				_interactive_beg = _cursor_pos;
				_interactive_end = _cursor_pos;

				select(_interactive_beg, _interactive_end, selection_mode::line);
			}

			_last_click_time = -1.0f;
		}
		else if (is_double_click)
		{
			if (!ctrl)
			{
				_cursor_pos = mouse_to_text_pos(ImGui::GetMousePos());
				_interactive_beg = _cursor_pos;
				_interactive_end = _cursor_pos;

				select(_interactive_beg, _interactive_end, selection_mode::word);
			}

			_last_click_time = ImGui::GetTime();
		}
		else if (is_clicked)
		{
			_cursor_pos = mouse_to_text_pos(ImGui::GetMousePos());
			_interactive_beg = _cursor_pos;
			_interactive_end = _cursor_pos;

			select(_interactive_beg, _interactive_end, ctrl ? selection_mode::word : selection_mode::normal);

			_last_click_time = ImGui::GetTime();
		}
		else if (ImGui::IsMouseDragging(0) && ImGui::IsMouseDown(0)) // Update selection while left mouse is dragging
		{
			io.WantCaptureMouse = true;

			_cursor_pos = mouse_to_text_pos(ImGui::GetMousePos());
			_interactive_end = _cursor_pos;

			select(_interactive_beg, _interactive_end, selection_mode::normal);
		}
	}

	colorize();

	const auto draw_list = ImGui::GetWindowDrawList();

	float longest_line = text_start;

	if (!_lines.empty())
	{
		const float font_scale = ImGui::GetFontSize() / ImGui::GetFont()->FontSize;
		const float space_size = ImGui::CalcTextSize(" ").x + font_scale;

		int line_no = static_cast<int>(floor(ImGui::GetScrollY() / char_advance.y));
		int line_max = std::max(0, std::min(static_cast<int>(_lines.size() - 1), line_no + static_cast<int>(floor((ImGui::GetScrollY() + ImGui::GetWindowContentRegionMax().y) / char_advance.y))));

		for (; line_no <= line_max; ++line_no, buf_end = buf)
		{
			const auto &line = _lines[line_no];

			// Position of the line number
			const ImVec2 line_screen_pos = ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + line_no * char_advance.y);
			// Position of the text inside the editor
			const ImVec2 text_screen_pos = ImVec2(line_screen_pos.x + text_start, line_screen_pos.y);

			longest_line = std::max(text_start + calc_text_distance_to_line_begin(text_pos(line_no, int(line.size()))), longest_line);

			text_pos lineStartCoord(line_no, 0);
			text_pos lineEndCoord(line_no, (int)line.size());

			// Draw selected area
			float selection_beg = -1.0f;
			float selection_end = -1.0f;

			assert(_select_beg <= _select_end);
			if (_select_beg <= lineEndCoord)
				selection_beg = _select_beg > lineStartCoord ? calc_text_distance_to_line_begin(_select_beg) : 0.0f;
			if (_select_end > lineStartCoord)
				selection_end = calc_text_distance_to_line_begin(_select_end < lineEndCoord ? _select_end : lineEndCoord);

			if (_select_end.line > line_no)
				selection_end += char_advance.x;

			if (selection_beg != -1 && selection_end != -1 && selection_beg < selection_end)
			{
				const ImVec2 beg = ImVec2(text_screen_pos.x + selection_beg, text_screen_pos.y);
				const ImVec2 end = ImVec2(text_screen_pos.x + selection_end, text_screen_pos.y + char_advance.y);

				draw_list->AddRectFilled(beg, end, _palette[color_selection]);
			}

			// Draw error markers
			if (auto it = _errors.find(line_no + 1); it != _errors.end())
			{
				const ImVec2 beg = ImVec2(line_screen_pos.x + ImGui::GetScrollX(), line_screen_pos.y);
				const ImVec2 end = ImVec2(line_screen_pos.x + ImGui::GetWindowContentRegionMax().x + 2.0f * ImGui::GetScrollX(), line_screen_pos.y + char_advance.y);

				draw_list->AddRectFilled(beg, end, _palette[color_error_marker]);

				if (ImGui::IsMouseHoveringRect(beg, end))
				{
					ImGui::BeginTooltip();
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
					ImGui::Text("Error at line %d:", it->first);
					ImGui::PopStyleColor();
					ImGui::Separator();
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.2f, 1.0f));
					ImGui::Text("%s", it->second.c_str());
					ImGui::PopStyleColor();
					ImGui::EndTooltip();
				}
			}

			// Draw line number (right aligned)
			snprintf(buf, 16, "%d  ", line_no + 1);

			draw_list->AddText(ImVec2(text_screen_pos.x - ImGui::CalcTextSize(buf).x, line_screen_pos.y), _palette[color_line_number], buf);

			// Highlight the current line (where the cursor is)
			if (_cursor_pos.line == line_no)
			{
				const bool is_focused = ImGui::IsWindowFocused();

				if (!has_selection())
				{
					const ImVec2 beg = ImVec2(line_screen_pos.x + ImGui::GetScrollX(), line_screen_pos.y);
					const ImVec2 end = ImVec2(line_screen_pos.x + ImGui::GetWindowContentRegionMax().x + 2.0f * ImGui::GetScrollX(), line_screen_pos.y + char_advance.y);

					draw_list->AddRectFilled(beg, end, _palette[uint8_t(is_focused ? color_palette::color_current_line_fill : color_palette::color_current_line_fill_inactive)]);
					draw_list->AddRect(beg, end, _palette[color_current_line_edge], 1.0f);
				}

				// Draw the cursor animation
				if (is_focused && io.OptCursorBlink && fmodf(_cursor_anim, 1.0f) <= 0.5f)
				{
					const float cx = calc_text_distance_to_line_begin(_cursor_pos);

					const ImVec2 beg = ImVec2(text_screen_pos.x + cx, line_screen_pos.y);
					const ImVec2 end = ImVec2(text_screen_pos.x + cx + (_overwrite ? char_advance.x : 1.0f), line_screen_pos.y + char_advance.y); // Larger cursor while overwriting

					draw_list->AddRectFilled(beg, end, _palette[color_cursor]);
				}
			}

			// Nothing to draw if the line is empty, so continue on
			if (line.empty())
				continue;

			// Draw colorized line text
			float text_offset = 0.0f;
			uint8_t prev_col = line[0].col;

			// Fill temporary buffer with glyph characters and commit it every time the color changes or a tab character is encountered
			for (const glyph &glyph : line)
			{
				if (buf != buf_end && (glyph.col != prev_col || glyph.c == '\t' || buf_end - buf >= 128))
				{
					draw_list->AddText(ImVec2(text_screen_pos.x + text_offset, text_screen_pos.y), _palette[prev_col], buf, buf_end);

					text_offset += ImGui::CalcTextSize(buf, buf_end).x + font_scale; buf_end = buf; // Reset temporary buffer
				}

				if (glyph.c != '\t')
					*buf_end++ = glyph.c;
				else
					text_offset = (font_scale + std::floor(1 + text_offset) / (float(_tab_size) * space_size)) * float(_tab_size) * space_size;

				prev_col = glyph.col;
			}

			// Draw any text still in the temporary buffer that was not yet committed
			if (buf != buf_end)
				draw_list->AddText(ImVec2(text_screen_pos.x + text_offset, text_screen_pos.y), _palette[prev_col], buf, buf_end);
		}
	}

	// Create dummy widget so a horizontal scrollbar appears
	ImGui::Dummy(ImVec2(longest_line + 2, _lines.size() * char_advance.y));

	if (_scroll_to_cursor)
	{
		const auto l = (int)ceil( ImGui::GetScrollX()                             / char_advance.x);
		const auto r = (int)ceil((ImGui::GetScrollX() + ImGui::GetWindowWidth())  / char_advance.x);
		const auto t = (int)ceil( ImGui::GetScrollY()                             / char_advance.y) + 1;
		const auto b = (int)ceil((ImGui::GetScrollY() + ImGui::GetWindowHeight()) / char_advance.y);

		const auto len = calc_text_distance_to_line_begin(_cursor_pos);

		if (_cursor_pos.line < t)
			ImGui::SetScrollY(std::max(0.0f, (_cursor_pos.line - 1) * char_advance.y));
		if (_cursor_pos.line > b - 4)
			ImGui::SetScrollY(std::max(0.0f, (_cursor_pos.line + 4) * char_advance.y - ImGui::GetWindowHeight()));
		if (len + text_start < l + 4)
			ImGui::SetScrollX(std::max(0.0f, len + text_start - 4));
		if (len + text_start > r - 4)
			ImGui::SetScrollX(std::max(0.0f, len + text_start + 4 - ImGui::GetWindowWidth()));

		ImGui::SetWindowFocus();

		_scroll_to_cursor = false;
	}

	ImGui::PopAllowKeyboardFocus();
	ImGui::EndChild();

	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

void code_editor_widget::select(const text_pos &beg, const text_pos &end, selection_mode mode)
{
	if (end > beg)
	{
		_select_beg = beg;
		_select_end = end;
	}
	else
	{
		_select_end = beg;
		_select_beg = end;
	}

	switch (mode)
	{
	case selection_mode::word:
		for (auto col = _lines[_select_beg.line][_select_beg.column - 1].col;
			_select_beg.column > 0; --_select_beg.column)
			if (col != _lines[_select_beg.line][_select_beg.column - 1].col)
				break;
		for (auto col = _lines[_select_end.line][_select_end.column - 1].col;
			_select_end.column < int(_lines[_select_end.line].size()); ++_select_end.column)
			if (col != _lines[_select_end.line][_select_end.column - 1].col)
				break;
		break;
	case selection_mode::line:
		_select_beg.column = 0;
		_select_end.column = _select_end.line < int(_lines.size()) ? int(_lines[_select_end.line].size()) : 0;
		break;
	}
}
void code_editor_widget::select_all()
{
	if (_lines.empty())
		return;

	select(text_pos(0, 0), text_pos(int(_lines.size() - 1), _lines.back().empty() ? 0 : int(_lines.back().size() - 1)));
}

void code_editor_widget::set_text(const std::string &text)
{
	_lines.clear();
	_lines.emplace_back();

	_errors.clear();

	for (char c : text)
	{
		if (c == '\r')
			continue; // Ignore the carriage return character
		else if (c == '\n')
			_lines.emplace_back();
		else
			_lines.back().push_back({ c, color_palette::color_default });
	}

	_colorize_line_beg = 0;
	_colorize_line_end = int(_lines.size());

}
void code_editor_widget::insert_text(const std::string &text)
{
	_colorize_line_beg = std::min(_colorize_line_beg, _cursor_pos.line);

	for (char c : text)
	{
		if (c == '\r')
			continue; // Ignore the carriage return character
		else if (c == '\n')
		{
			auto &new_line = insert_line(_cursor_pos.line + 1);

			if (_cursor_pos.column < int(_lines[_cursor_pos.line].size()))
			{
				auto &line = _lines[_cursor_pos.line];
				new_line.insert(new_line.begin(), line.begin() + _cursor_pos.column, line.end());
				line.erase(line.begin() + _cursor_pos.column, line.end());
			}

			_cursor_pos.line++;
			_cursor_pos.column = 0;
			continue;
		}

		auto &line = _lines[_cursor_pos.line];
		line.insert(line.begin() + _cursor_pos.column, { c, color_palette::color_default });

		_cursor_pos.column++;
	}

	select(_cursor_pos, _cursor_pos);

	_scroll_to_cursor = true;

	_colorize_line_end = std::max(_colorize_line_end, _cursor_pos.line + 1);
}
void code_editor_widget::insert_character(char c, bool shift)
{
	if (has_selection())
	{
		if (c == '\t') // Pressing tab with a selection indents the entire selection
		{
			auto beg = _select_beg;
			auto end = _select_end;

			_colorize_line_beg = std::min(_colorize_line_beg, beg.line);
			_colorize_line_end = std::max(_colorize_line_end, end.line + 1);

			beg.column = 0;
			if (end.column == 0 && end.line > 0)
				end.column = _lines[--end.line].size();

			for (int i = beg.line; i <= end.line; i++)
			{
				auto &line = _lines[i];

				if (shift)
				{
					if (line[0].c == '\t')
					{
						line.erase(line.begin());
						if (i == end.line && end.column > 0)
							end.column--;
						_scroll_to_cursor = true;
					}
					else for (int j = 0; j < _tab_size && line[0].c == ' '; j++) // Do the same for spaces
					{
						line.erase(line.begin());
						if (i == end.line && end.column > 0)
							end.column--;
						_scroll_to_cursor = true;
					}
				}
				else
				{
					line.insert(line.begin(), { '\t', color_palette::color_background });
					if (i == end.line)
						++end.column;
					_scroll_to_cursor = true;
				}
			}
			return;
		}

		// Otherwise overwrite the selection
		delete_selection();
	}

	assert(!_lines.empty());

	_colorize_line_beg = std::min(_colorize_line_beg, _cursor_pos.line);

	if (c == '\n')
	{
		auto &new_line = insert_line(_cursor_pos.line + 1), &line = _lines[_cursor_pos.line];

		// Auto indentation
		for (size_t it = 0; it < line.size() && isblank(line[it].c); ++it)
			new_line.push_back(line[it]);
		const int indentation = int(new_line.size());

		new_line.insert(new_line.end(), line.begin() + _cursor_pos.column, line.end());
		line.erase(line.begin() + _cursor_pos.column, line.begin() + line.size());

		_cursor_pos.line++;
		_cursor_pos.column = indentation;
	}
	else
	{
		auto &line = _lines[_cursor_pos.line];

		if (_overwrite && int(line.size()) > _cursor_pos.column)
			line[_cursor_pos.column] = { c, color_palette::color_default };
		else
			line.insert(line.begin() + _cursor_pos.column, { c, color_palette::color_default });

		_cursor_pos.column++;
	}

	_scroll_to_cursor = true;

	_colorize_line_end = std::max(_colorize_line_end, _cursor_pos.line + 1);
}
std::vector<code_editor_widget::glyph> &code_editor_widget::insert_line(int line_pos)
{
	_colorize_line_beg = std::min(_colorize_line_beg, line_pos);
	_colorize_line_end = std::max(_colorize_line_end, line_pos + 1);

	// Move all error markers after the new line one up
	std::unordered_map<int, std::string> errors;
	errors.reserve(_errors.size());
	for (auto &i : _errors)
		errors.insert({ i.first >= line_pos ? i.first + 1 : i.first, i.second });
	_errors = std::move(errors);

	return *_lines.emplace(_lines.begin() + line_pos);
}

std::string code_editor_widget::get_text() const
{
	return get_text(0, int(_lines.size()));
}
std::string code_editor_widget::get_text(const text_pos &beg, const text_pos &end) const
{
	std::string result;

	int prev_line_no = beg.line;

	for (auto it = beg; it <= end;)
	{
		if (prev_line_no != it.line && it.line < int(_lines.size()))
			result.push_back('\n');

		if (it == end)
			break;

		prev_line_no = it.line;

		const auto &line = _lines[it.line];
		if (!line.empty() && it.column < int(line.size()))
			result.push_back(line[it.column].c);

		if (it.column + 1 < int(line.size()))
			++it.column;
		else
			++it.line, it.column = 0;
	}

	return result;
}
std::string code_editor_widget::get_selected_text() const
{
	return get_text(_select_beg, _select_end);
}
std::string code_editor_widget::get_current_line_text() const
{
	return get_text(text_pos(_cursor_pos.line, 0), text_pos(_cursor_pos.line, int(_lines[_cursor_pos.line].size())));
}

void code_editor_widget::delete_next()
{
	if (_lines.empty())
		return;

	if (has_selection())
	{
		delete_selection();
		return;
	}

	auto &line = _lines[_cursor_pos.line];

	// If at end of line, move next line into the current one
	if (_cursor_pos.column == (int)line.size())
	{
		if (_cursor_pos.line == (int)_lines.size() - 1)
			return; // This already is the last line

		// Copy next line into current line
		const auto &next_line = _lines[_cursor_pos.line + 1];
		line.insert(line.end(), next_line.begin(), next_line.end());

		// Remove next line
		delete_lines(_cursor_pos.line + 1, _cursor_pos.line + 1);
	}
	else
	{
		line.erase(line.begin() + _cursor_pos.column);

		_colorize_line_beg = std::min(_colorize_line_beg, _cursor_pos.line);
		_colorize_line_end = std::max(_colorize_line_end, _cursor_pos.line + 1);
	}
}
void code_editor_widget::delete_previous()
{
	if (_lines.empty())
		return;

	if (has_selection())
	{
		delete_selection();
		return;
	}

	// If at beginning of line, move previous line into the current one
	if (_cursor_pos.column == 0)
	{
		if (_cursor_pos.line == 0)
			return; // This already is the first line

		auto &line = _lines[_cursor_pos.line];
		auto &prev_line = _lines[_cursor_pos.line - 1];
		_cursor_pos.column = (int)prev_line.size();

		// Copy current line into previous line
		prev_line.insert(prev_line.end(), line.begin(), line.end());

		// Remove current line
		delete_lines(_cursor_pos.line, _cursor_pos.line);

		--_cursor_pos.line;
	}
	else
	{
		auto &line = _lines[_cursor_pos.line];
		--_cursor_pos.column;

		if (_cursor_pos.column < (int)line.size())
			line.erase(line.begin() + _cursor_pos.column);

		_colorize_line_beg = std::min(_colorize_line_beg, _cursor_pos.line);
		_colorize_line_end = std::max(_colorize_line_end, _cursor_pos.line + 1);
	}

	_scroll_to_cursor = true;
}
void code_editor_widget::delete_selection()
{
	if (_select_end == _select_beg)
		return;

	assert(has_selection());

	delete_range(_select_beg, _select_end);

	select(_select_beg, _select_beg);
	_cursor_pos = _select_beg;
}
void code_editor_widget::delete_range(const text_pos &beg, const text_pos &end)
{
	if (end == beg)
		return;

	assert(end > beg);
	assert(end.column >= 0);

	_colorize_line_beg = std::min(_colorize_line_beg, beg.line);
	_colorize_line_end = std::max(_colorize_line_end, end.line + 1);

	if (beg.line == end.line)
	{
		auto &line = _lines[beg.line];

		if (end.column >= (int)line.size())
			line.erase(line.begin() + beg.column, line.end());
		else
			line.erase(line.begin() + beg.column, line.begin() + end.column);
	}
	else
	{
		auto &beg_line = _lines[beg.line];
		auto &end_line = _lines[end.line];

		beg_line.erase(beg_line.begin() + beg.column, beg_line.end());
		end_line.erase(end_line.begin(), end_line.begin() + end.column);

		if (beg.line < end.line)
			beg_line.insert(beg_line.end(), end_line.begin(), end_line.end());

		if (beg.line < end.line)
			delete_lines(beg.line + 1, end.line);

		assert(!_lines.empty());
	}
}
void code_editor_widget::delete_lines(int first_line, int last_line)
{
	_colorize_line_beg = std::min(_colorize_line_beg, first_line);
	_colorize_line_end = std::max(_colorize_line_end, last_line + 1);

	// Move all error markers after the deleted lines down
	std::unordered_map<int, std::string> errors;
	errors.reserve(_errors.size());
	for (auto &i : _errors)
		if (i.first < first_line && i.first > last_line)
			errors.insert({ i.first > last_line ? i.first - (last_line - first_line) : i.first, i.second });
	_errors = std::move(errors);

	_lines.erase(_lines.begin() + first_line, _lines.begin() + last_line + 1);
}

void code_editor_widget::clipboard_copy()
{
	if (has_selection())
	{
		ImGui::SetClipboardText(get_selected_text().c_str());
	}
	else if (!_lines.empty()) // Copy current line if there is no selection
	{
		std::string line_text;
		line_text.reserve(_lines[_cursor_pos.line].size());
		for (const glyph &glyph : _lines[_cursor_pos.line])
			line_text.push_back(glyph.c);

		ImGui::SetClipboardText(line_text.c_str());
	}
}
void code_editor_widget::clipboard_cut()
{
	if (!has_selection())
		return;

	clipboard_copy();
	delete_selection();
}
void code_editor_widget::clipboard_paste()
{
	const char *const text = ImGui::GetClipboardText();

	if (text == nullptr || *text == '\0')
		return;

	if (has_selection())
		delete_selection();

	insert_text(text);
}

void code_editor_widget::move_up(unsigned int amount, bool selection)
{
	if (_lines.empty())
		return;

	const auto prev_pos = _cursor_pos;
	_cursor_pos.line = std::max(_cursor_pos.line - int(amount), 0);

	if (selection)
	{
		if (prev_pos == _interactive_beg)
			_interactive_beg = _cursor_pos;
		else if (prev_pos == _interactive_end)
			_interactive_end = _cursor_pos;
		else
			_interactive_beg = _cursor_pos,
			_interactive_end = prev_pos;
	}
	else
	{
		_interactive_beg = _cursor_pos;
		_interactive_end = _cursor_pos;
	}

	select(_interactive_beg, _interactive_end);

	_scroll_to_cursor = true;
}
void code_editor_widget::move_down(unsigned int amount, bool selection)
{
	if (_lines.empty())
		return;

	assert(_cursor_pos.column >= 0);

	const auto prev_pos = _cursor_pos;
	_cursor_pos.line = std::min(_cursor_pos.line + int(amount), int(_lines.size() - 1));

	if (selection)
	{
		if (prev_pos == _interactive_beg)
			_interactive_beg = _cursor_pos;
		else if (prev_pos == _interactive_end)
			_interactive_end = _cursor_pos;
		else
			_interactive_beg = prev_pos,
			_interactive_end = _cursor_pos;
	}
	else
	{
		_interactive_beg = _cursor_pos;
		_interactive_end = _cursor_pos;
	}

	select(_interactive_beg, _interactive_end);

	_scroll_to_cursor = true;
}
void code_editor_widget::move_left(unsigned int amount, bool selection, bool word_mode)
{
	if (_lines.empty())
		return;

	const auto prev_pos = _cursor_pos;

	while (amount-- > 0)
	{
		if (_cursor_pos.column == 0) // At the beginning of the current line, so move on to previous
		{
			if (_cursor_pos.line == 0)
				break;

			_cursor_pos.line--;
			_cursor_pos.column = int(_lines[_cursor_pos.line].size());
		}
		else
		{
			_cursor_pos.column--;

			// TODO
			//if (word_mode)
			//	_cursor_pos = FindWordStart(_cursor_pos);
		}
	}

	assert(_cursor_pos.line >= 0);
	assert(_cursor_pos.column >= 0);

	if (selection)
	{
		if (prev_pos == _interactive_beg)
			_interactive_beg = _cursor_pos;
		else if (prev_pos == _interactive_end)
			_interactive_end = _cursor_pos;
		else
			_interactive_beg = _cursor_pos,
			_interactive_end = prev_pos;
	}
	else
	{
		_interactive_beg = _cursor_pos;
		_interactive_end = _cursor_pos;
	}

	select(_interactive_beg, _interactive_end, selection && word_mode ? selection_mode::word : selection_mode::normal);

	_scroll_to_cursor = true;
}
void code_editor_widget::move_right(unsigned int amount, bool selection, bool word_mode)
{
	if (_lines.empty())
		return;

	const auto prev_pos = _cursor_pos;

	while (amount-- > 0)
	{
		auto &line = _lines[_cursor_pos.line];

		if (_cursor_pos.column >= int(line.size() - 1)) // At the end of the current line, so move on to next
		{
			if (_cursor_pos.line >= int(_lines.size() - 1))
				break;

			_cursor_pos.line++;
			_cursor_pos.column = 0;
		}
		else
		{
			_cursor_pos.column++;

			// TODO
			//if (word_mode)
			//	_cursor_pos = FindWordEnd(_cursor_pos);
		}
	}

	assert(_cursor_pos.line < int(_lines.size()));
	assert(_cursor_pos.column < int(_lines[_cursor_pos.line].size()) || _lines[_cursor_pos.line].empty());

	if (selection)
	{
		if (prev_pos == _interactive_end)
			_interactive_end = _cursor_pos;
		else if (prev_pos == _interactive_beg)
			_interactive_beg = _cursor_pos;
		else
			_interactive_beg = prev_pos,
			_interactive_end = _cursor_pos;
	}
	else
	{
		_interactive_beg = _interactive_end = _cursor_pos;
	}

	select(_interactive_beg, _interactive_end, selection && word_mode ? selection_mode::word : selection_mode::normal);

	_scroll_to_cursor = true;
}
void code_editor_widget::move_top(bool selection)
{
	const auto prev_pos = _cursor_pos;
	_cursor_pos = text_pos(0, 0);

	if (selection)
	{
		if (_interactive_beg > _interactive_end)
			std::swap(_interactive_beg, _interactive_end);
		if (prev_pos != _interactive_beg)
			_interactive_end = _interactive_beg;

		_interactive_beg = _cursor_pos;
	}
	else
	{
		_interactive_beg = _cursor_pos;
		_interactive_end = _cursor_pos;
	}

	select(_interactive_beg, _interactive_end);

	_scroll_to_cursor = true;
}
void code_editor_widget::move_bottom(bool selection)
{
	const auto prev_pos = _cursor_pos;
	_cursor_pos = text_pos(int(_lines.size() - 1), 0);

	if (selection)
	{
		if (_interactive_beg > _interactive_end)
			std::swap(_interactive_beg, _interactive_end);
		if (prev_pos != _interactive_end)
			_interactive_beg = _interactive_end;

		_interactive_end = _cursor_pos;
	}
	else
	{
		_interactive_beg = _cursor_pos;
		_interactive_end = _cursor_pos;
	}

	select(_interactive_beg, _interactive_end);

	_scroll_to_cursor = true;
}
void code_editor_widget::move_home(bool selection)
{
	const auto prev_pos = _cursor_pos;
	_cursor_pos = text_pos(_cursor_pos.line, 0);

	if (_cursor_pos == prev_pos)
		return;

	if (selection)
	{
		if (prev_pos == _interactive_beg)
			_interactive_beg = _cursor_pos;
		else if (prev_pos == _interactive_end)
			_interactive_end = _cursor_pos;
		else
			_interactive_beg = _cursor_pos,
			_interactive_end = prev_pos;
	}
	else
	{
		_interactive_beg = _interactive_end = _cursor_pos;
	}

	select(_interactive_beg, _interactive_end);
}
void code_editor_widget::move_end(bool selection)
{
	const auto prev_pos = _cursor_pos;
	_cursor_pos = text_pos(_cursor_pos.line, int(_lines[prev_pos.line].size()));

	if (_cursor_pos == prev_pos)
		return;

	if (selection)
	{
		if (prev_pos == _interactive_end)
			_interactive_end = _cursor_pos;
		else if (prev_pos == _interactive_beg)
			_interactive_beg = _cursor_pos;
		else
			_interactive_beg = prev_pos,
			_interactive_end = _cursor_pos;
	}
	else
	{
		_interactive_beg = _interactive_end = _cursor_pos;
	}

	select(_interactive_beg, _interactive_end);
}

void code_editor_widget::colorize()
{
	if (_lines.empty() || _colorize_line_beg >= _colorize_line_end)
		return;

	const int from = _colorize_line_beg, to = std::min(from + 1000, _colorize_line_end);
	_colorize_line_beg = to;

	if (_colorize_line_beg == _colorize_line_end)
	{
		_colorize_line_beg = std::numeric_limits<int>::max();
		_colorize_line_end = 0;
	}

	std::string input_string;
	for (size_t l = from; l < size_t(to) && l < _lines.size(); ++l, input_string.push_back('\n'))
		for (size_t k = 0; k < _lines[l].size(); ++k)
			input_string.push_back(_lines[l][k].c);

	reshadefx::lexer lexer(input_string, false, true, false, false, false);

	for (reshadefx::token tok; (tok = lexer.lex()).id != reshadefx::tokenid::end_of_file;)
	{
		uint8_t col = color_default;

		switch (tok.id)
		{
		case reshadefx::tokenid::exclaim:
		case reshadefx::tokenid::percent:
		case reshadefx::tokenid::ampersand:
		case reshadefx::tokenid::parenthesis_open:
		case reshadefx::tokenid::parenthesis_close:
		case reshadefx::tokenid::star:
		case reshadefx::tokenid::plus:
		case reshadefx::tokenid::comma:
		case reshadefx::tokenid::minus:
		case reshadefx::tokenid::dot:
		case reshadefx::tokenid::slash:
		case reshadefx::tokenid::colon:
		case reshadefx::tokenid::semicolon:
		case reshadefx::tokenid::less:
		case reshadefx::tokenid::equal:
		case reshadefx::tokenid::greater:
		case reshadefx::tokenid::question:
		case reshadefx::tokenid::bracket_open:
		case reshadefx::tokenid::backslash:
		case reshadefx::tokenid::bracket_close:
		case reshadefx::tokenid::caret:
		case reshadefx::tokenid::brace_open:
		case reshadefx::tokenid::pipe:
		case reshadefx::tokenid::brace_close:
		case reshadefx::tokenid::tilde:
		case reshadefx::tokenid::exclaim_equal:
		case reshadefx::tokenid::percent_equal:
		case reshadefx::tokenid::ampersand_ampersand:
		case reshadefx::tokenid::ampersand_equal:
		case reshadefx::tokenid::star_equal:
		case reshadefx::tokenid::plus_plus:
		case reshadefx::tokenid::plus_equal:
		case reshadefx::tokenid::minus_minus:
		case reshadefx::tokenid::minus_equal:
		case reshadefx::tokenid::arrow:
		case reshadefx::tokenid::ellipsis:
		case reshadefx::tokenid::slash_equal:
		case reshadefx::tokenid::colon_colon:
		case reshadefx::tokenid::less_less_equal:
		case reshadefx::tokenid::less_less:
		case reshadefx::tokenid::less_equal:
		case reshadefx::tokenid::equal_equal:
		case reshadefx::tokenid::greater_greater_equal:
		case reshadefx::tokenid::greater_greater:
		case reshadefx::tokenid::greater_equal:
		case reshadefx::tokenid::caret_equal:
		case reshadefx::tokenid::pipe_equal:
		case reshadefx::tokenid::pipe_pipe:
			col = color_punctuation;
			break;
		case reshadefx::tokenid::identifier:
			if (tok.literal_as_string == "Width" ||
				tok.literal_as_string == "Height" ||
				tok.literal_as_string == "Format" ||
				tok.literal_as_string == "MipLevels" ||
				tok.literal_as_string == "Texture" ||
				tok.literal_as_string == "MinFilter" ||
				tok.literal_as_string == "MagFilter" ||
				tok.literal_as_string == "MipFilter" ||
				tok.literal_as_string == "MipLODBias" ||
				tok.literal_as_string == "MaxMipLevel" ||
				tok.literal_as_string == "abs" ||
				tok.literal_as_string == "tex2D" ||
				tok.literal_as_string == "tex2Dlod" ||
				tok.literal_as_string == "tex2Dfetch")
				col = color_known_identifier;
			else
				col = color_identifier;
			break;
		case reshadefx::tokenid::int_literal:
		case reshadefx::tokenid::uint_literal:
		case reshadefx::tokenid::float_literal:
		case reshadefx::tokenid::double_literal:
			col = color_number_literal;
			break;
		case reshadefx::tokenid::string_literal:
			col = color_string_literal;
			break;
		case reshadefx::tokenid::true_literal:
		case reshadefx::tokenid::false_literal:
		case reshadefx::tokenid::namespace_:
		case reshadefx::tokenid::struct_:
		case reshadefx::tokenid::technique:
		case reshadefx::tokenid::pass:
		case reshadefx::tokenid::for_:
		case reshadefx::tokenid::while_:
		case reshadefx::tokenid::do_:
		case reshadefx::tokenid::if_:
		case reshadefx::tokenid::else_:
		case reshadefx::tokenid::switch_:
		case reshadefx::tokenid::case_:
		case reshadefx::tokenid::default_:
		case reshadefx::tokenid::break_:
		case reshadefx::tokenid::continue_:
		case reshadefx::tokenid::return_:
		case reshadefx::tokenid::discard_:
		case reshadefx::tokenid::extern_:
		case reshadefx::tokenid::static_:
		case reshadefx::tokenid::uniform_:
		case reshadefx::tokenid::volatile_:
		case reshadefx::tokenid::precise:
		case reshadefx::tokenid::in:
		case reshadefx::tokenid::out:
		case reshadefx::tokenid::inout:
		case reshadefx::tokenid::const_:
		case reshadefx::tokenid::linear:
		case reshadefx::tokenid::noperspective:
		case reshadefx::tokenid::centroid:
		case reshadefx::tokenid::nointerpolation:
		case reshadefx::tokenid::void_:
		case reshadefx::tokenid::bool_:
		case reshadefx::tokenid::bool2:
		case reshadefx::tokenid::bool3:
		case reshadefx::tokenid::bool4:
		case reshadefx::tokenid::bool2x2:
		case reshadefx::tokenid::bool3x3:
		case reshadefx::tokenid::bool4x4:
		case reshadefx::tokenid::int_:
		case reshadefx::tokenid::int2:
		case reshadefx::tokenid::int3:
		case reshadefx::tokenid::int4:
		case reshadefx::tokenid::int2x2:
		case reshadefx::tokenid::int3x3:
		case reshadefx::tokenid::int4x4:
		case reshadefx::tokenid::uint_:
		case reshadefx::tokenid::uint2:
		case reshadefx::tokenid::uint3:
		case reshadefx::tokenid::uint4:
		case reshadefx::tokenid::uint2x2:
		case reshadefx::tokenid::uint3x3:
		case reshadefx::tokenid::uint4x4:
		case reshadefx::tokenid::float_:
		case reshadefx::tokenid::float2:
		case reshadefx::tokenid::float3:
		case reshadefx::tokenid::float4:
		case reshadefx::tokenid::float2x2:
		case reshadefx::tokenid::float3x3:
		case reshadefx::tokenid::float4x4:
		case reshadefx::tokenid::vector:
		case reshadefx::tokenid::matrix:
		case reshadefx::tokenid::string_:
		case reshadefx::tokenid::texture:
		case reshadefx::tokenid::sampler:
			col = color_keyword;
			break;
		case reshadefx::tokenid::hash_def:
		case reshadefx::tokenid::hash_undef:
		case reshadefx::tokenid::hash_if:
		case reshadefx::tokenid::hash_ifdef:
		case reshadefx::tokenid::hash_ifndef:
		case reshadefx::tokenid::hash_else:
		case reshadefx::tokenid::hash_elif:
		case reshadefx::tokenid::hash_endif:
		case reshadefx::tokenid::hash_error:
		case reshadefx::tokenid::hash_warning:
		case reshadefx::tokenid::hash_pragma:
		case reshadefx::tokenid::hash_include:
		case reshadefx::tokenid::hash_unknown:
			col = color_preprocessor;
			tok.offset--; // Add # to token
			tok.length++;
			break;
		case reshadefx::tokenid::single_line_comment:
			col = color_comment;
			break;
		case reshadefx::tokenid::multi_line_comment:
			col = color_multiline_comment;
			break;
		}

		int line = from + tok.location.line - 1;
		int column = tok.location.column - 1;

		for (size_t k = 0; k < tok.length; ++k)
		{
			if (column == int(_lines[line].size()))
			{
				line++;
				column = 0;
				continue;
			}

			_lines[line][column++].col = col;
		}
	}
}
