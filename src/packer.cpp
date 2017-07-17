#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>

struct MimeType {
    std::string type;
    bool isBinary;
};
std::map<std::string, MimeType> mimetypeMap = {
    {"html", {"text/html", false}}
   ,{"htm", {"text/html", false}}
   ,{"txt", {"text/plain", false}}
   ,{"js", {"application/javascript", false}}
   ,{"css", {"text/css", false}}
   ,{"jpg", {"image/jpeg", true}}
   ,{"jpeg", {"image/jpeg", true}}
   ,{"gif", {"image/gif", true}}
   ,{"png", {"image/png", true}}
};

void processFile(std::ostream& ofhdr, std::ostream& ofsrc, std::ostream& vmap, std::ostream& fmap, const std::string& ofname, const std::string& rpath, const std::string& rfname){
    auto ifname = rpath + rfname;
    std::cout << "-Processing:" << ifname << ":" << ofname << std::endl;
    std::string vname;
    std::string ext;
    for(auto& ch : rfname){
        switch(ch){
            case '/':
            case ':':
            case '\\':
                ext = "";
                vname += "_";
                break;
            case '.':
                ext = "";
                vname += "_";
                break;
            case '-':
                ext += ch;
                vname += "_";
                break;
            default:
                ext += ch;
                vname += ch;
                break;
        }
    }

    std::string mimetype = "text/plain";
    bool isBinary = true;
    auto mit = mimetypeMap.find(ext);
    if(mit != mimetypeMap.end()){
        mimetype = mit->second.type;
        isBinary = mit->second.isBinary;
    }

    ofsrc << "const unsigned char " << vname << "[] = {" << std::endl;
    size_t tlen = 0;
    unsigned char buf[17];
    std::ifstream ifs(ifname, std::ios::binary);
    if (!ifs.is_open()) {
        std::cout << "Unable to open file:" << ifname << std::endl;
        exit(1);
    }
    std::string s;
    while(!ifs.eof()) {
        ifs.read((char*)buf, 16);
        auto len = ifs.gcount();
        buf[len] = 0;

        if(isBinary){
            // add binary file chunks as-is
            s += std::string((const char*)buf, len);
        }else{
            // replace consequtive whitespace with single space in text files, to reduce size
            int lastch = 0;
            for(size_t i = 0; i < len; ++i){
                int ch = buf[i];
                switch(ch){
                case ' ':
                case '\t':
                    if(lastch != ' '){
                        s += ' ';
                    }
                    lastch = ' ';
                    break;
                default:
                    s += ch;
                    lastch = ch;
                    break;
                }
            }
        }

        size_t slen = 0;
        while((s.length() >= 16) || (len < 16)){
            size_t x = 0;
            for (auto& ch : s) {
                char hex[10];
                snprintf(hex, 10, "%02x", (unsigned char)ch);
                ofsrc << "0x" << hex << ", ";
                ++slen;
                if (++x == 16) {
                    break;
                }
            }
            while(x < 16){
                ofsrc << "      ";
                ++x;
            }

            x = 0;
            ofsrc << "/* ";
            for (auto& ch : s) {
                if (isprint(ch) && (ch != '/') && (ch != '*')) {
                    ofsrc << ch;
                }
                else {
                    ofsrc << ' ';
                }
                if (++x == 16) {
                    break;
                }
            }
            s = s.substr(x);
            while(x < 16){
                ofsrc << ' ';
                ++x;
            }
            ofsrc << " */";
            ofsrc << std::endl;
            if (s.length() == 0) {
                break;
            }
        }
        tlen += slen;
        if(len < 16) {
            break;
        }
    }
    ofsrc << "0" << std::endl;
    ofsrc << "};" << std::endl;

    vmap << "std::tuple<const unsigned char*, size_t, std::string, bool> " << vname << "_Tuple {" << vname << ", " << tlen << ", \"" << mimetype << "\", " << isBinary << "};" << std::endl;
    fmap << "{\"" << ofname << "\", " << vname << "_Tuple}" << std::endl;
}

int main(int argc, const char* argv[]){
    std::string ofdir;
    std::string ofname;
    std::string resfile;

    bool showHelp = true;
    if(argc > 1){
        for(int i = 1; i < argc;++i){
            auto args = std::string(argv[i]);
            if(args == "-v"){
                if(i >= (argc-1)){
                    std::cout << "Invalid variable name" << std::endl;
                    break;
                }
                ++i;
                ofname = argv[i];
            }else if(args == "-d"){
                if(i >= (argc-1)){
                    std::cout << "Invalid output directory" << std::endl;
                    break;
                }
                ++i;
                ofdir = argv[i];
            }else{
                resfile = args;
            }
        }
        showHelp = false;
    }
    if(showHelp){
        std::cout << argv[0] << " -d <outputdir> -v <filename> <resfile>" << std::endl;
        return 0;
    }

    std::string rpath;
    auto rpos = resfile.find_last_of("/\\");
    if (rpos != std::string::npos) {
        rpath = resfile.substr(0, rpos) + "/";
    }
    std::ifstream rfs(resfile);
    if (!rfs) {
        std::cout << "unable to open resource file:" << resfile << std::endl;
        return 1;
    }

    auto ofhdrname = ofdir + "/" + ofname + ".hpp";
    auto ofsrcname = ofdir + "/" + ofname + ".cpp";
    std::cout << "Generating:" << ofhdrname << " & " << ofsrcname << std::endl;
    std::ofstream ofhdr(ofhdrname);
    if(!ofhdr){
        std::cout << "unable to open:" << ofhdrname << std::endl;
        return 1;
    }

    std::ofstream ofsrc(ofsrcname);
    if(!ofsrc){
        std::cout << "unable to open:" << ofsrcname << std::endl;
        return 1;
    }

    std::ostringstream vmap;
    std::ostringstream fmap;
    std::string sep = "  ";
    ofhdr << "#include <map>" << std::endl;
    ofhdr << "#include <string>" << std::endl;
    ofhdr << "extern std::map<std::string, std::tuple<const unsigned char*, size_t, std::string, bool>> " << ofname << ";" << std::endl;
    ofsrc << "#include \"" << ofname << ".hpp\"" << std::endl;
    ofsrc << "namespace {" << std::endl;
    while (!rfs.eof()) {
        std::string ofname;
        std::string ifname;
        rfs >> std::quoted(ofname) >> std::quoted(ifname);
        if ((ofname.length() > 0) && (ifname.length() > 0)) {
            fmap << sep;
            processFile(ofhdr, ofsrc, vmap, fmap, ofname, rpath, ifname);
            sep = ", ";
        }
    }
    ofsrc << "} // namespace" << std::endl;
    ofsrc << std::endl;
    ofsrc << vmap.str();
    ofsrc << "std::map<std::string, std::tuple<const unsigned char*, size_t, std::string, bool>> " << ofname << " = {" << std::endl;
    ofsrc << fmap.str();
    ofsrc << "};" << std::endl;
    return 0;
}
