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

void processFile(std::ostream& ofhdr, std::ostream& ofsrc, std::ostream& vmap, std::ostream& fmap, const std::string& ifname, const std::string& vpfx){
    std::cout << "-Processing:" << ifname << std::endl;
    std::string fname;
    std::string vname;
    std::string ext;
    for(auto& ch : ifname){
        switch(ch){
            case '/':
            case ':':
            case '\\':
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
            bool inStar = false;
            ofsrc << "/* ";
            for (auto& ch : s) {
                if (isprint(ch)) {
                    if (inStar) {
                        if (ch == '/') {
                            ofsrc << ' ';
                        }
                        else {
                            ofsrc << ch;
                        }
                        inStar = false;
                    }
                    else if (ch == '*') {
                        inStar = true;
                    }
                    else {
                        ofsrc << ch;
                    }
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

    vmap << "std::tuple<const unsigned char*, size_t, std::string, bool> " << vname << "Tuple {" << vname << ", " << tlen << ", \"" << mimetype << "\", " << isBinary << "};" << std::endl;
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
    for(auto& ifname : ifnameList){
        fmap << sep;
        processFile(ofhdr, ofsrc, vmap, fmap, ifname, vpfx);
        sep = ", ";
    }
    ofsrc << "} // namespace" << std::endl;
    ofsrc << std::endl;
    ofsrc << vmap.str();
    ofsrc << "std::map<std::string, std::tuple<const unsigned char*, size_t, std::string, bool>> " << ofname << " = {" << std::endl;
    ofsrc << fmap.str();
    ofsrc << "};" << std::endl;
    return 0;
}
