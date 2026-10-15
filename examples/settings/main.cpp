#include "settings.hpp"
#include <cstdlib>
#include <iostream>
int wmain(int argc,wchar_t** argv){
    try{
        settings::DesktopOptions options;
        std::filesystem::path file;
        wchar_t* local=nullptr;std::size_t length=0;
        if(_wdupenv_s(&local,&length,L"LOCALAPPDATA")==0&&local){file=std::filesystem::path(local)/"LcarsUI"/"settings.conf";std::free(local);}
        else file="settings.conf";
        bool explicit_data=false;
        for(int i=1;i<argc;++i){
            std::wstring_view arg=argv[i];
            if(arg==L"--smoke")options.smoke_test=true;
            else if(arg==L"--data"&&i+1<argc){file=argv[++i];explicit_data=true;}
            else if(arg==L"--screenshot"&&i+1<argc)options.screenshot=argv[++i];
            else throw std::invalid_argument("Usage: lcars_settings_web [--data file] [--smoke] [--screenshot file.bmp]");
        }
        if(options.smoke_test && !explicit_data)throw std::invalid_argument("--smoke requires an explicit --data path");
        settings::App app(file);
#ifdef LCARS_APPEARANCE_material
        const auto& appearance=lcars::material_appearance();
#else
        const auto& appearance=lcars::web_appearance();
#endif
        return settings::run_desktop(app.ui,appearance,[&]{return app.screen();},options);
    }catch(const std::exception& e){std::cerr<<"LcarsUI: "<<e.what()<<'\n';return 1;}
}
