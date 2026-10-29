"""Use the actual facade painters and the frozen C++ field renderer."""
import pathlib
import subprocess
import sys
root = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
s = (root / 'src/activities/util/KeyboardEntryActivity.cpp').read_text()
(out / 'keyboard-paint.inc').write_text('\n'.join(function(s, signature) for signature in [
    'void KeyboardEntryActivity::paintKey(', 'void KeyboardEntryActivity::paintRow(', 'bool KeyboardEntryActivity::validText(',
    'void KeyboardEntryActivity::paintText(']))
s = subprocess.check_output(['git', '-C', str(root), 'show',
    'ba6860d8:src/activities/util/KeyboardEntryActivity.cpp'], text=True)
parts = [function(s, signature) for signature in [
    'size_t KeyboardEntryActivity::utf8Prev(', 'size_t KeyboardEntryActivity::utf8Next(',
    'std::string KeyboardEntryActivity::displayTextForCurrentState(',
    'int KeyboardEntryActivity::measureRange(', 'bool KeyboardEntryActivity::rangeIsRtl(',
    'int KeyboardEntryActivity::lineBreakEnd(']]
body = function(s, 'void KeyboardEntryActivity::render(')
body = body[:body.index('  if (hintVisible && cursorMode')] + '}\n'
body = body.replace('render(RenderLock&&)', 'render()')
body = body.replace('  const Rect content = UITheme::getContentArea(renderer);',
                    '  const FieldRect content{l.content.x,l.content.y,l.content.w,l.content.h};')
body = body.replace('  const auto& metrics = UITheme::getInstance().getMetrics();', '')
body = body.replace('  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());', '')
for name, field in dict(topPadding='top_padding', headerHeight='header_height',
    verticalSpacing='spacing', keyboardVerticalOffset='vertical_offset',
    sideButtonHintsWidth='side_width', keyboardTextFieldWidthPercent='text_percent',
    keyboardCenteredText='centered').items():
    body = body.replace('metrics.' + name, 'l.' + field)
body = body.replace('gpio.deviceIsX3()', 'l.x3')
a = body.index('  GUI.drawTextField(')
b = body.index(';', a) + 1
body = body[:a] + '''  renderer.drawLine(content.x + lineMargin, inputStartY + inputHeight + lineHeight + l.spacing,
      content.x + lineMargin + content.width - 2 * lineMargin + l.field_end,
      inputStartY + inputHeight + lineHeight + l.spacing,
      cursorMode ? l.field_cursor : l.field_normal, true);''' + body[b:]
parts.append(body)
(out / 'keyboard-field.inc').write_text('\n'.join(parts).replace('KeyboardEntryActivity::', 'OriginalField::'))
