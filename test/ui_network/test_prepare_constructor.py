"""Keep brace-initialized members inside an extracted native constructor."""
import prepare

source='''Widget::Widget(int value)
    : rows{}, text{}, enabled(value != 0) { publish(value); }
void Widget::publish(int) {}
'''
expected=source[:source.index('\nvoid Widget::publish')]
assert prepare.extract(source,'Widget','Widget')==expected
print('Native fixture constructor extraction retains empty member initializers PASS')
