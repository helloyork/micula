#include <micula/image_view.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstdlib>

using namespace micula;
static void Check(bool ok,const char *message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM initialization");
    wchar_t directory[MAX_PATH],name[MAX_PATH];
    Check(GetTempPathW(MAX_PATH,directory)&&GetTempFileNameW(directory,L"mic",0,name),"temporary image path");
    const std::wstring path=name;
    BITMAPFILEHEADER file={};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
    file.bfSize=file.bfOffBits+64*32*4;
    BITMAPINFOHEADER info={};info.biSize=sizeof(info);info.biWidth=64;info.biHeight=32;
    info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
    {std::ofstream stream(std::filesystem::path(path),std::ios::binary);
        const std::vector<unsigned> pixels(64*32,0xffff0000u);
        stream.write(reinterpret_cast<const char *>(&file),sizeof(file));
        stream.write(reinterpret_cast<const char *>(&info),sizeof(info));
        stream.write(reinterpret_cast<const char *>(pixels.data()),pixels.size()*4);
        Check(bool(stream),"write image fixture");}
    {
        Surface owner;
        Check(!owner.Image(path,0),"image without a drawing context must not load");
        const auto &graphics=Device();Check(graphics.Ready(),"graphics device");
        Check(SUCCEEDED(graphics.d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&owner.dc)),"offscreen context");
        auto *full=owner.Image(path,0);auto *half=owner.Image(path,32);auto *thumbnail=owner.Image(path,16);
        Check(full&&half&&thumbnail,"decode image at three widths");
        Check(full->GetPixelSize().width==64&&full->GetPixelSize().height==32,"full resolution");
        Check(half->GetPixelSize().width==32&&half->GetPixelSize().height==16,"half resolution");
        Check(thumbnail->GetPixelSize().width==16&&thumbnail->GetPixelSize().height==8,"thumbnail resolution");
        Check(owner.Image(path,32)==half,"reuse matching decode width");
        const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
        ID2D1Bitmap1 *target=nullptr;
        Check(SUCCEEDED(owner.dc->CreateBitmap(D2D1::SizeU(100,100),nullptr,0,&props,&target)),"offscreen target");
        owner.dc->SetTarget(target);
        ImageView image(path,100,100,32);image.host=&owner;image.rect=Rect(0,0,100,100);
        const auto wanted=image.Measure(Room{});Check(wanted.w.size==100&&wanted.h.size==100,"fixed image dimensions");
        Painter painter;painter.rt=owner.dc;
        owner.dc->BeginDraw();owner.dc->Clear(D2D1::ColorF(0,0,0,0));image.Paint(painter);
        Check(SUCCEEDED(owner.dc->EndDraw()),"paint image");
        auto readProps=props;readProps.bitmapOptions=D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        ID2D1Bitmap1 *readback=nullptr;
        Check(SUCCEEDED(owner.dc->CreateBitmap(D2D1::SizeU(100,100),nullptr,0,&readProps,&readback)),"readback bitmap");
        Check(SUCCEEDED(readback->CopyFromBitmap(nullptr,target,nullptr)),"readback copy");
        D2D1_MAPPED_RECT mapped={};Check(SUCCEEDED(readback->Map(D2D1_MAP_OPTIONS_READ,&mapped)),"readback map");
        Check(mapped.bits[5*mapped.pitch+50*4+3]==0,"aspect-fit letterbox remains transparent");
        Check(mapped.bits[50*mapped.pitch+50*4+2]>240&&mapped.bits[50*mapped.pitch+50*4+3]>240,"image centered in destination");
        readback->Unmap();readback->Release();
        image.SetSource(path+L".missing");owner.dc->BeginDraw();image.Paint(painter);
        Check(SUCCEEDED(owner.dc->EndDraw()),"missing image is harmless");
        owner.dc->SetTarget(nullptr);target->Release();owner.ReleaseDevice();
        Check(owner.images.empty(),"image resources released");
    }
    DeleteFileW(path.c_str());CoUninitialize();
    std::puts("PASS: image decoding, per-width cache, aspect-fit pixels, source changes and cleanup");
}
