#include <micula/drop_down.h>
#include <micula/view.h>
#include <cstdio>
#include <cstdlib>

using namespace micula;
static void Check(bool yes,const char *why){if(!yes){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool Near(float a,float b){return std::fabs(a-b)<.02f;}
struct Fixture {
    View root; Fonts fonts; DropDown *dd;
    Fixture(float height,float y,int count,int selected){
        root.rect=Rect(0,0,600,height);
        std::vector<std::wstring> opts(count,L"Option");
        dd=root.Add(new DropDown(opts,selected,[](int){}));
        dd->rect=Rect(100,y,200,32);dd->SetOpen(true);
    }
    ~Fixture(){auto *popup=dd->flyout;dd->flyout=nullptr;if(popup)root.Remove(popup);}
    void Frame(float dt=.016f){ArrangeSubtree(&root,fonts);root.Tick(dt);}
    void Rest(){for(int i=0;i<150;++i)Frame();}
    float Mark(){return dd->mark->drawn.top;}
};
int main(){
    Animations(true);
    // Fractional space on both sides must not be combined into another row.
    for(float height:{400.f,413.f,600.f})for(float y:{9.f,87.f,159.f,251.f}){
        if(y+32>height-8)continue;
        for(int selected:{0,1,10,39}){
            Fixture f(height,y,40,selected);f.Rest();
            const float above=std::floor((y-8)/32);
            const float below=std::floor((height-8-y-32)/32);
            const float view=std::clamp(float(selected)-above,0.f,40.f-(above+1+below));
            Check(Near(Height(f.dd->scroll->rect),8+32*(above+1+below)),"long panel must expand whole rows from anchor");
            Check(Near(f.dd->mark->rect.top,y+(selected-view-above)*32),"mark must follow the selected visible row");
            Check(Near(f.dd->scroll->scroll,view*32),"viewport must start on a whole row");
            Check(f.dd->scroll->rect.top>=3.99f&&f.dd->scroll->rect.bottom<=height-3.99f,"panel must fit room");
            const auto panel=f.dd->scroll->rect;
            f.dd->Choose((selected+1)%40);f.Rest();
            Check(SameRect(panel,f.dd->scroll->rect),"long viewport must not move or shrink with selection");
        }
    }
    // The nav example: Row 26 -> 27 reaches the bottom scroll limit; Row 3 -> 2
    // reaches the top one. Keep the same 17-row viewport and animate the mark.
    for(auto step : {std::pair<int,int>{25,26},{2,1}}){
        Fixture f(600,87,40,step.first);f.Rest();
        const auto panel=f.dd->scroll->rect;const float start=f.Mark(),view=f.dd->scroll->scroll;
        f.dd->Choose(step.second);f.Frame();const float target=f.dd->mark->rect.top;
        Check(SameRect(panel,f.dd->scroll->rect),"end transition must preserve panel edges");
        Check(Near(view,f.dd->scroll->scroll),"end transition must leave the visible options stationary");
        Check(!Near(start,target),"end transition must move the marker target");
        Check(f.Mark()>std::min(start,target)&&f.Mark()<std::max(start,target),"end marker must interpolate");
        f.Rest();Check(Near(f.Mark(),target),"end marker must arrive at selected row");
    }
    // A short list stays complete, and its clamped mark must traverse the rows.
    Fixture clamped(400,13,3,0);clamped.Rest();
    Check(Near(Height(clamped.dd->scroll->rect),104),"short list must show all rows");
    const float from=clamped.Mark();clamped.dd->Choose(1);clamped.Frame();
    const float to=clamped.dd->mark->rect.top;
    Check(to>from+1,"clamped choice must move the mark target");
    Check(clamped.Mark()>from&&clamped.Mark()<to,"first frame must interpolate, not snap");
    clamped.Rest();Check(Near(clamped.Mark(),to),"mark must finish on chosen row");
    clamped.dd->Choose(0);clamped.Frame();
    Check(clamped.Mark()<to&&clamped.Mark()>clamped.dd->mark->rect.top,"reverse selection must also glide");
    clamped.Rest();
    // Without clamping, the panel moves while the mark remains fixed.
    Fixture free(600,240,5,1);free.Rest();const float fixed=free.Mark();
    free.dd->Choose(2);
    for(int i=0;i<100;++i){free.Frame();Check(Near(free.Mark(),fixed),"unclamped mark must remain stationary throughout glide");}
    Animations(false);clamped.dd->Choose(2);clamped.Frame();
    Check(Near(clamped.Mark(),clamped.dd->mark->rect.top),"animations off must place immediately");
    std::puts("PASS: anchored integer rows, clamped mark glide, stationary free mark, animations off");
}
