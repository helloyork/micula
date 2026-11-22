#include <micula/window.h>
#include <cstdio>
#include <cstdlib>
using namespace micula;
static void Check(bool ok,const char *message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
struct IdleSurface:Surface {
    int starts=0,stops=0;
    void BeginPump()override{++starts;}
    void EndPump()override{++stops;}
};
int main(){
    {App empty;bool drew=true;Check(!empty.Step(false,&drew)&&!drew,"empty loop ends without drawing");empty.WaitFrame();}
    IdleSurface first,second;App app;app.Add(first);
    bool drew=true;Check(app.Step(false,&drew)&&!drew&&app.Running(),"nonblocking idle turn");
    Check(first.starts==1&&!app.WantsFrame(),"initial pump and idle frame query");
    PostThreadMessageW(GetCurrentThreadId(),WM_APP+42,0,0);
    Check(app.Step(false)&&first.starts==1,"repeated turn does not restart pump");
    MSG queued={};Check(!PeekMessageW(&queued,nullptr,WM_APP+42,WM_APP+42,PM_NOREMOVE),"queued messages drained");
    app.Add(second);Check(second.starts==1,"late surface joins running pump");
    app.Quit(37);Check(!app.Step(false)&&app.ExitCode()==37,"quit code preserved");
    app.Finish();Check(first.stops==1&&second.stops==1&&!app.Running(),"finish stops pumps");
    std::puts("PASS: nonblocking loop, queued messages, late surfaces, exit code and finish");
}
