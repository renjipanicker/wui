#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>

std::map<std::string, std::string> mimetypeMap = {
    {"html", "text/html"}
   ,{"htm", "text/html"}
   ,{"txt", "text/plain"}
   ,{"js", "application/javascript"}
   ,{"css", "text/css"}
   ,{"jpg", "image/jpeg"}
   ,{"jpeg", "image/jpeg"}
   ,{"gif", "image/gif"}
   ,{"png", "image/png"}
};

void processFile(std::ostream& ofhdr, std::ostream& ofsrc, std::ostream& vmap, std::ostream& fmap, const std::string& ifname, const std::string& vpfx){
    std::cout << "-Processing:" << ifname << std::endl;
    std::string fname;
    std::string vname;
    std::string ext;
    for(auto& ch : ifname){
        switch(ch){
            case '/':
                ext = "";
                fname = "";
                vname += "_";
                break;
            case '.':
                ext = "";
                fname += ch;
                vname += "_";
                break;
            case '-':
            case '\\':
                ext += ch;
                fname += ch;
                vname += "_";
                break;
            default:
                ext += ch;
                fname += ch;
                vname += ch;
                break;
        }
    }

    std::string mimetype = "text/plain";
    auto mit = mimetypeMap.find(ext);
    if(mit != mimetypeMap.end()){
        mimetype = mit->second;
    }

    ofsrc << "const unsigned char " << vname << "[] = {" << std::endl;
    size_t tlen = 0;
    unsigned char buf[17];
    std::ifstream ifs(ifname);
    while(!ifs.eof()) {
        ifs.read((char*)buf, 16);
        auto len = ifs.gcount();
        buf[len] = 0;
        std::string s;
        for(int i = 0; i < len; ++i){
            ofsrc << "0x" << std::hex << std::setw(2) << std::setfill('0') << (unsigned int)buf[i] << ", ";
            if(buf[i] != '/'){
                s += buf[i];
            }
        }
        ofsrc << "/*" << s << "*/" << std::endl;
        tlen += len;
        ofsrc << std::endl;
    }
    ofsrc << "0" << std::endl;
    ofsrc << "};" << std::endl;

    vmap << "std::tuple<const unsigned char*, size_t, std::string> " << vname << "Tuple {" << vname << ", " << tlen << ", \"" << mimetype << "\"};" << std::endl;
    fmap << "{\"" << vpfx << fname << "\", " << vname << "Tuple}" << std::endl;
}

int main(int argc, const char* argv[]){
    std::string ofdir;
    std::string ofname;
    std::string vpfx;
    std::vector<std::string> ifnameList;

    bool showHelp = true;
    if(argc > 1){
        for(int i = 1; i < argc;++i){
            auto args = std::string(argv[i]);
            if(args == "-v"){
                if(i >= (argc-1)){
                    std::cout << "Invalid arguments" << std::endl;
                    break;
                }
                ++i;
                ofname = argv[i];
            }else if(args == "-d"){
                if(i >= (argc-1)){
                    std::cout << "Invalid directory" << std::endl;
                    break;
                }
                ++i;
                ofdir = argv[i];
            }else if(args == "-p"){
                if(i >= (argc-1)){
                    std::cout << "Invalid prefix" << std::endl;
                    break;
                }
                ++i;
                vpfx = argv[i];
            }else{
                ifnameList.push_back(args);
            }
        }
        showHelp = false;
    }
    if(showHelp){
        std::cout << argv[0] << "-d <outputdir> -v <varname> <infile-list>" << std::endl;
        return 0;
    }

    std::cout << "Generating:" << ofname << std::endl;
    std::ofstream ofhdr(ofdir + "/" + ofname + ".hpp");
    std::ofstream ofsrc(ofdir + "/" + ofname + ".cpp");
    std::ostringstream vmap;
    std::ostringstream fmap;
    std::string sep = "  ";
    ofhdr << "#include <map>" << std::endl;
    ofhdr << "#include <string>" << std::endl;
    ofhdr << "extern std::map<std::string, std::tuple<const unsigned char*, size_t, std::string>> " << ofname << ";" << std::endl;
    ofsrc << "#include \"" << ofname << ".hpp\"" << std::endl;
    ofsrc << "namespace {" << std::endl;
    for(auto& ifname : ifnameList){
        fmap << sep;
        processFile(ofhdr, ofsrc, vmap, fmap, ifname, vpfx);
        sep = ", ";
    }
    ofsrc << "} // namespace" << std::endl;
    ofsrc << std::endl;
    ofsrc << vmap.str();
    ofsrc << "std::map<std::string, std::tuple<const unsigned char*, size_t, std::string>> " << ofname << " = {" << std::endl;
    ofsrc << fmap.str();
    ofsrc << "};" << std::endl;
    return 0;
}
