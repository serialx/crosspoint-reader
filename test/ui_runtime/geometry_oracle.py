"""Capture GfxRenderer's integer geometry before replacing its UI callers."""
import pathlib
import subprocess
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def record(top_corners=False, wide_lines=False):
    source = (ROOT / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
    methods = [function(source, 'void GfxRenderer::drawLine(int x1, int y1, int x2, int y2, const bool state)'),
               function(source, 'void GfxRenderer::drawRect(const int x, const int y, const int width, const int height, const int lineWidth,'),
               function(source, 'void GfxRenderer::drawArc('),
               function(source, 'void GfxRenderer::drawRoundedRect(const int x, const int y, const int width, const int height, const int lineWidth,\n                                  const int cornerRadius, bool roundTopLeft'),
               'template<Color color>\n' + function(source, 'void GfxRenderer::fillArc('),
               function(source, 'void GfxRenderer::fillRoundedRect(const int x, const int y, const int width, const int height, const int cornerRadius,\n                                  bool roundTopLeft')]
    methods.append(function(source, 'void GfxRenderer::drawLine(int x1, int y1, int x2, int y2, const int lineWidth,'))
    prefix = r'''
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
enum class Color { Clear, Black, White, LightGray, DarkGray };
class GfxRenderer {
public:
 mutable uint8_t bytes[96];
 int orientation=0;
 int getScreenWidth() const { return orientation % 2 == 0 ? 24 : 32; }
 int getScreenHeight() const { return orientation % 2 == 0 ? 32 : 24; }
 struct Cache { bool isScanning() const { return false; } };
 Cache* fontCacheManager_ = nullptr;
 void drawPixel(int x,int y,bool black) const {
   if(x<0||y<0||x>=getScreenWidth()||y>=getScreenHeight()) return;
   int px=x,py=y;
   switch(orientation) {
    case 0:px=y;py=23-x;break;
    case 1:px=31-x;py=23-y;break;
    case 2:px=31-y;py=x;break;
   }
   const int i=py*4+px/8; const uint8_t bit=0x80>>(px%8);
   if(black) bytes[i]&=~bit; else bytes[i]|=bit;
 }
 void fillRect(int x,int y,int w,int h,bool black) const {
   for(int j=y;j<y+h;j++) for(int i=x;i<x+w;i++) drawPixel(i,j,black);
 }
 void fillRectDither(int x,int y,int w,int h,Color c) const {
   if(c==Color::Clear) return;
   for(int j=y;j<y+h;j++) for(int i=x;i<x+w;i++)
    drawPixel(i,j,c==Color::Black||(c==Color::LightGray && i%2==0 && j%2==0)||(c==Color::DarkGray && (i+j)%2==0));
 }
 void drawLine(int,int,int,int,bool) const;
 void drawLine(int,int,int,int,int,bool) const;
 void drawRect(int,int,int,int,int,bool) const;
 void drawArc(int,int,int,int,int,int,bool) const;
 void drawRoundedRect(int,int,int,int,int,int,bool,bool,bool,bool,bool) const;
 template<Color color> void fillArc(int,int,int,int,int) const;
 void fillRoundedRect(int,int,int,int,int,bool,bool,bool,bool,Color) const;
};
'''
    suffix = r'''
int main() {
 for(int orientation=0;orientation<4;orientation++) {
  for(int radius : {0,1,6,12}) {
   for(int paint=0;paint<4;paint++) {
    GfxRenderer r;r.orientation=orientation;
    std::fill_n(r.bytes,96,255);
    r.fillRoundedRect(-2,1,23,17,radius,true,true,true,true,static_cast<Color>(paint+1));
    r.drawRoundedRect(3,7,19,12,2,radius,true,true,true,true,true);
    r.drawLine(1,1,16,22,true);
    std::fwrite(r.bytes,1,96,stdout);
   }
  }
 }
}
'''
    if wide_lines:
        suffix = r'''
int main() {
 for(int orientation=0;orientation<4;orientation++)
  for(int width : {0,1,2,3})
   for(int paint=0;paint<2;paint++)
    for(auto delta : {std::pair{15,0},std::pair{0,18},std::pair{15,21},std::pair{-15,21},std::pair{15,-21},std::pair{-15,-21}}) {
     GfxRenderer r;r.orientation=orientation;
     std::fill_n(r.bytes,96,paint==0?255:0);
     r.drawLine(8,9,8+delta.first,9+delta.second,std::max(width,1),paint==0);
     std::fwrite(r.bytes,1,96,stdout);
    }
}
'''
    if top_corners:
        suffix = suffix.replace("radius,true,true,true,true,true)", "radius,true,true,false,false,true)")
    with tempfile.TemporaryDirectory() as tmp:
        cpp = pathlib.Path(tmp) / 'oracle.cpp'
        binary = pathlib.Path(tmp) / 'oracle'
        cpp.write_text(prefix + '\n'.join(methods) + suffix)
        subprocess.run(['c++', '-std=c++20', str(cpp), '-o', str(binary)], check=True)
        return subprocess.check_output([str(binary)])


if __name__ == '__main__':
    (HERE / 'geometry.golden').write_bytes(record())
