#pragma once
#include <iostream>
#include <sstream>
#include <map>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <assert.h>

namespace s {
    namespace js {
        /// \brief context for converting incoming function call to native
        struct conversion_context {
            std::vector<std::string> args;
            std::string retv;
            inline conversion_context(const std::vector<std::string>& a) : args(a) {}
        };

        /// \brief base class for convertor from string to native
        template <typename T, typename DerT>
        struct convertorbase {
            static inline T convertFromJS(const std::string& str) {
                T val;
                std::istringstream ss(str);
                ss >> val;
                return val;
            }

            static inline T convertParamFromJS(conversion_context& ctx, size_t& idx) {
                return DerT::convertFromJS(ctx.args.at(idx++));
            }

            static inline auto convertToJS(conversion_context& ctx, const T& t) {
                std::ostringstream ss;
                ss << t;
                return ss.str();
            }

            static inline std::string convertToNative(const std::string& var) {
                return "String(" + var + ")";
            }
        };

        /// \brief convertor from string to native
        template <typename T>
        struct convertor : public convertorbase<T, convertor<T>> {
            static inline std::string getJsTypeName();
        };

        template <>
        struct convertor<int> : public convertorbase<int, convertor<int>>{
            static inline std::string getJsTypeName() {
                return "number";
            }
        };

        template <>
        struct convertor<bool> : public convertorbase<bool, convertor<bool>> {
            static inline bool convertFromJS(const std::string& str) {
                if(str == "true"){
                    return true;
                }
                return false;
            }

            static inline std::string convertToJS(conversion_context& ctx, const bool& t) {
                if(t){
                    return "true";
                }
                return "false";
            }

            static inline std::string getJsTypeName() {
                return "bool";
            }
        };

        template <>
        struct convertor<std::string> : public convertorbase<std::string, convertor<std::string>> {
            static inline auto convertFromJS(const std::string& str) {
                auto rv = str.substr(1, str.length() - 2); // strip double-quotes from ends
                return rv;
            }

            static inline auto convertToJS(conversion_context& ctx, const std::string& t) {
                return t;
            }

            static inline std::string getJsTypeName() {
                return "string";
            }

            static inline std::string convertToNative(const std::string& var) {
                return "'\"' + String(" + var + ") + '\"'";
            }
        };

        template <typename T>
        struct convertor<std::vector<T>> : public convertorbase<std::vector<T>, convertor<std::vector<T>>> {
            static inline std::vector<T> convertFromJS(const std::string& str) {
                auto spos = str.find('\1');
                decltype(spos) lpos = 0;
                std::vector<T> rv;
                while(lpos != std::string::npos){
                    std::string elem;
                    if(spos == std::string::npos){
                        elem = str.substr(lpos);
                        lpos = spos;
                    }else{
                        elem = str.substr(lpos, spos - lpos);
                        lpos = spos+1;
                    }

                    rv.push_back(convertor<T>::convertFromJS(elem));
                    spos = str.find('\1', lpos);
                }
                return rv;
            }

            static inline auto convertToJS(conversion_context& ctx, const std::vector<T>& t) {
                std::string sep;
                std::string rv = "[";
                for(auto& v : t){
                    rv += sep;
                    rv += convertor<T>::convertToJS(v);
                    sep = ",";
                }
                rv += "]";
                return rv;
            }

            static inline std::string getJsTypeName() {
                return "array";
            }

            static inline std::string convertToNative(const std::string& var) {
                std::ostringstream ss;
                ss << "function(){" << std::endl;
                ss << "      var lst = '';" << std::endl;
                ss << "      var sep = '';" << std::endl;
                ss << "      for(var i in " + var + ") {" << std::endl;
                ss << "        lst += sep;" << std::endl;
                ss << "        lst += " << convertor<T>::convertToNative(var + "[i]") << ";" << std::endl;
                ss << "        sep = '\1';" << std::endl;
                ss << "      }" << std::endl;
                ss << "      return lst;" << std::endl;
                ss << "    }()";
                return ss.str();
            }
        };

        /////////////////////////////////////////////////
        /// T: decay'ed type
        template <typename T>
        static inline auto getParamStatement(const size_t& idx, const std::string& t, std::ostringstream& ss){
            std::ostringstream ts;
            ts << "p" << idx;
            auto var = ts.str();
            ss << "    /*" << convertor<T>::getJsTypeName() << "*/" << std::endl;
            ss << "    rv.push(" << convertor<T>::convertToNative(var) << ");" << std::endl;
            return var;
        }

        template<typename...>
        struct ParamStatementListGenerator;

        template<typename T, typename...A>
        struct ParamStatementListGenerator<T, A...> {
             static void call(std::ostringstream& ss, std::ostringstream& sp) {
                auto var = getParamStatement<typename std::decay<T>::type>(sizeof...(A), "", ss);
                sp << var;
                if(sizeof...(A) > 0){
                    sp << ", ";
                    ParamStatementListGenerator<A...>::call(ss, sp);
                }
             }
        };

        template<>
        struct ParamStatementListGenerator<> {
             static void call(std::ostringstream& /*ss*/, std::ostringstream& /*sp*/) {
             }
        };

        template <typename Ret, typename... A>
        static inline std::string getFunctionBody(const std::string& name){
            std::ostringstream ss;
            std::ostringstream sp;
            ParamStatementListGenerator<A...>::call(ss, sp);

            std::ostringstream os;
            os << "function(" << sp.str() << "){" << std::endl;
            os << "    var rv = new Array();" << std::endl;
            os << ss.str() << std::endl;
            os << "    var vv = this.__nobj__.invoke('" << name << "', rv);" << std::endl;
            os << "    return _wui_convertFromNative(vv);" << std::endl;
            os << "  }";
            return os.str();
        }

        /////////////////////////////////////////////////
        template <typename F>
        struct Invoker {};

        template <typename Cls, typename Res, typename... Args>
        struct Invoker<Res (Cls::*)(Args...)> {
            typedef Res (Cls::*FnT)(Args...);
            static inline void afn_run(Cls fnx, conversion_context& ctx) {
                size_t idx = 0;
                auto rv = fnx(convertor<typename std::decay<Args>::type>::convertParamFromJS(ctx, idx)...);
                ctx.retv = convertor<typename std::decay<decltype(rv)>::type>::convertToJS(ctx, rv);
            }
            static inline void obj_run(Cls obj, FnT fn, conversion_context& ctx) {
                size_t idx = 0;
                auto rv = (obj.*fn)(convertor<typename std::decay<Args>::type>::convertParamFromJS(ctx, idx)...);
                ctx.retv = convertor<typename std::decay<decltype(rv)>::type>::convertToJS(ctx, rv);
            }
        };

        template <typename Cls, typename... Args>
        struct Invoker<void (Cls::*)(Args...)> {
            typedef void (Cls::*FnT)(Args...);
            static inline void afn_run(Cls fnx, conversion_context& ctx) {
                size_t idx = 0;
                fnx(convertor<typename std::decay<Args>::type>::convertParamFromJS(ctx, idx)...);
            }
            static inline void obj_run(Cls obj, FnT fn, conversion_context& ctx) {
                size_t idx = 0;
                (obj.*fn)(convertor<typename std::decay<Args>::type>::convertParamFromJS(ctx, idx)...);
            }
        };

        /////////////////////////////////////////////////
        template <typename Cls, typename Ret, typename... Args>
        struct classdefbase {
            typedef Ret (Cls::*FnT)(Args...);
            static inline void afn_invoke(Cls f, conversion_context& ctx) {
                return Invoker<FnT>::afn_run(f, ctx);
            }
            static inline void obj_invoke(Cls obj, FnT fn, conversion_context& ctx) {
                return Invoker<FnT>::obj_run(obj, fn, ctx);
            }
            static inline auto stringy(const std::string& name) {
                return getFunctionBody<Ret, Args...>(name);
            }
        };

        template <typename T>
        struct class_def {};

        // member function
        template <typename Cls, typename Ret, typename... Args>
        struct class_def<Ret (Cls::*)(Args...)> : public classdefbase<Cls, Ret, Args...> {};

        // const member function
        template <typename Cls, typename Ret, typename... Args>
        struct class_def<Ret (Cls::*)(Args...) const> : public classdefbase<Cls, Ret, Args...> {};

        /////////////////////////////////////////////////
        /// \brief generic for member functions
        template <typename T, typename=void>
        struct get_signature_impl {
            using cdef = class_def<T>;
        };

        /// \brief specialised for anonymous functions
        template <typename T>
        struct get_signature_impl<T, typename std::enable_if<std::is_class<T>::value>::type> {
            typedef decltype(&std::remove_reference<T>::type::operator()) FnT;
            using cdef = class_def<FnT>;
        };

        /////////////////////////////////////////////////
        template <typename ObjT>
        struct klass {
            std::string name_;
            std::string str_;
            std::map<std::string, std::function<void(s::js::conversion_context& ctx, ObjT&)>> fnl_;

            inline klass(const std::string& name) : name_(name) {
                std::stringstream ss_;
                ss_ << "function cls_" << name_ << "(nobj) {" << std::endl;
                ss_ << "  this.__nobj__ = nobj;";
                str_ += ss_.str();
            }

            template<typename FnT>
            inline auto& addBody(const std::string& name, FnT /*fnx*/){
                auto body = s::js::get_signature_impl<FnT>::cdef::stringy(name);
                std::ostringstream ss_;
                ss_ << std::endl;
                ss_ << "  this." + name + " = " << body;
                str_ += ss_.str();
                return *this;
            }

            template<typename FnT>
            inline auto& method(const std::string& name, FnT fnx){
                fnl_[name] = [fnx](s::js::conversion_context& ctx, ObjT& obj) {
                    s::js::get_signature_impl<FnT>::cdef::obj_invoke(obj, fnx, ctx);
                };
                return addBody(name, fnx);
            }

            template<typename FnT>
            inline auto& function(const std::string& name, FnT fnx){
                fnl_[name] = [fnx](s::js::conversion_context& ctx, ObjT& /*obj*/) {
                    s::js::get_signature_impl<FnT>::cdef::afn_invoke(fnx, ctx);
                };
                return addBody(name, fnx);
            }

            template <typename P>
            struct PropType {};

            template <typename Cls, typename Res>
            struct PropType<Res Cls::*> {
                typedef Res type;
            };

            template<typename PropT>
            inline auto& property(const std::string& name, PropT p){
                typedef typename std::decay<typename PropType<PropT>::type>::type PT;
                auto gn = "get_" + name;
                fnl_[gn] = [p](s::js::conversion_context& ctx, ObjT& obj) {
                    auto& rv = (obj.*p);
                    ctx.retv = convertor<typename std::decay<decltype(rv)>::type>::convertToJS(ctx, rv);
                };

                auto sn = "set_" + name;
                fnl_[sn] = [p](s::js::conversion_context& ctx, ObjT& obj) {
                    size_t idx = 0; // need this coz idx parameter to convertParamFromJS() is a non-const ref
                    (obj.*p) = convertor<PT>::convertParamFromJS(ctx, idx);
                };

                std::ostringstream sp;
                auto var = getParamStatement<PT>(0, convertor<PT>::getJsTypeName(), sp);

                std::ostringstream ss_;
                ss_ << std::endl;
                ss_ << "  Object.defineProperty(this, '" + name + "', {" << std::endl;
                ss_ << "    get: function() {" << std::endl;
                ss_ << "      var rv = new Array();" << std::endl;
                ss_ << "      var v = this.__nobj__.invoke('" << gn << "', rv);" << std::endl;
                ss_ << "      return v;" << std::endl;
                ss_ << "    }," << std::endl;
                ss_ << "    set: function(p0) {" << std::endl;
                ss_ << "      var rv = new Array();" << std::endl;
                ss_ << sp.str() << std::endl;
                ss_ << "      this.__nobj__.invoke('" << sn << "', rv);" << std::endl;
                ss_ << "    }" << std::endl;
                ss_ << "  });";
                str_ += ss_.str();
                return *this;
            }

            inline auto& end(){
                std::stringstream ss_;
                ss_ << std::endl;
                ss_ << "}";
                str_ += ss_.str();
                return *this;
            }

            inline auto str() const {
                return str_;
            }

            inline std::string invoke(ObjT& obj, const std::string& fn, const std::vector<std::string>& params) const {
                auto fit = fnl_.find(fn);
                if (fit == fnl_.end()) {
                    throw std::runtime_error(std::string("unknown functionz:") + fn);
                }

                s::js::conversion_context ctx(params);
                fit->second(ctx, obj);
                return ctx.retv;
            }
        };

        /////////////////////////////////////////////////
        struct objectbase {
            std::string name;
            std::string nname;
            inline objectbase(const std::string& n) : name(n), nname("__" + n + "__") {}

            virtual std::string invoke(const std::string& fn, const std::vector<std::string>& params) = 0;
        }; // objectbase

        struct object : public objectbase {
            s::js::klass<object> kls;
            inline object(const std::string& n) : objectbase(n), kls(n) {}

            struct FunctionInserter {
                object& obj;
                std::string name;

                template <typename FnT>
                inline auto& operator=(FnT fnx) {
                    obj.kls.function(name, fnx);
                    return *this;
                }

                inline FunctionInserter& operator=(const FunctionInserter&) = delete;
                inline FunctionInserter(object& o, const std::string& n) : obj(o), name(n) {}
            }; // FunctionInserter

            inline auto fn(const std::string& n) {
                return FunctionInserter(*this, n);
            }

            std::string invoke(const std::string& fn, const std::vector<std::string>& params) override {
                return kls.invoke(*this, fn, params);
            }
        };

        template <typename ObjT>
        struct objectT : public objectbase {
            const s::js::klass<ObjT>& kls;
            ObjT& obj;
            inline objectT(const std::string& n, const s::js::klass<ObjT>& k, ObjT& o) : objectbase(n), kls(k), obj(o) {}

            std::string invoke(const std::string& fn, const std::vector<std::string>& params) override {
                return kls.invoke(obj, fn, params);
            }
        }; // objectT

    } // ns js

    /// \brief web UI
    namespace wui {
        /// \brief cross-platform menu functions
        /// \todo: to implement
        struct menu {
            std::string name;
            inline menu(const std::string& n) : name(n) {}
            inline menu& submenu(const std::string& /*n*/, std::function<void()> /*fn*/) {
                return *this;
            }
        };

        /// \brief cross-platform systray functions
        /// \todo: to implement
        struct systray {
        };

        /// \brief page content location
        enum class ContentSourceType {
            Embedded, /// \brief content is embedded in the executable
            Resource, /// \brief content is in the OS defined resource location for the application
            Standard, /// \brief content is from net or local file
        };

        class window {
        public:
            struct Impl;

        private:
            std::unique_ptr<Impl> impl_;
            std::map<std::string, std::unique_ptr<s::js::objectbase>> objList_;

        public:
            inline Impl& impl();

        // event handlers
        public:
            std::function<void()> onOpen;
            std::function<void()> onClose;
            std::function<void(const std::string&)> onLoad;
            std::function<void(const std::string&)> onLog;
            std::function<void()> onPreferences;
            std::function<void()> onNewFile;
            std::function<void()> onOpenFile;
            std::function<void()> onSaveFile;
            std::function<void(const std::string&)> onSaveAsFile;
            std::function<void()> onCloseFile;
            std::function<void()> onAbout;
            std::function<void(const std::string&)> onHelp; // (context)

        public:
            void setContentSourceEmbedded(const std::map<std::string, std::tuple<const unsigned char*, size_t, std::string>>& lst);
            void setContentSourceResource(const std::string& path);
            bool open();
            void setDefaultMenu();
            void setMenu(const menu& m);
            /// \todo: sethscroll, setvscroll, settitle, setpos, etc

            window();
            ~window();

            void go(const std::string& url);

            template<typename ObjT>
            inline void addClass(const s::js::klass<ObjT>& kls) {
                eval(kls.str());
            }

            /// \brief add native object into DOM
            void addNativeObject(s::js::objectbase& jo, const std::string& body);

            inline auto getBody(const std::string& objname, const std::string& fndef, const std::string& nativename) {
                std::string body;
                body += "window." + objname + " = new " + fndef + "(" + nativename + ");";
                return body;
            }

            template <typename ObjT>
            inline void setObject(const s::js::klass<ObjT>& kls, const std::string& name, ObjT& obj) {
                objList_[name] = std::make_unique<s::js::objectT<ObjT>>(name, kls, obj);
                auto& jo = objList_[name];
                auto body = getBody(name, kls.name_, jo->nname);
                addNativeObject(*jo, body);
            }

            inline auto& newObject(const std::string& name) {
                objList_[name] = std::make_unique<s::js::object>(name);
                auto& jo = objList_[name];
                return dynamic_cast<s::js::object&>(*jo);
            }

            inline void addObject(s::js::object& obj) {
                obj.kls.end();
                auto body = getBody(obj.name, obj.kls.str(), obj.nname);
                addNativeObject(obj, body);
            }

            inline auto& getObject(const std::string& name) {
                auto oit = objList_.find(name);
                if (oit == objList_.end()) {
                    throw std::runtime_error(std::string("unknown object:") + name);
                }

                return *(oit->second);
            }

            /// \brief eval a string
            /// should always be called on main thread
            void eval(const std::string& str);
        };
    } // wui

    /// \brief application main loop, instance checker, etc
    class application {
    public:
        class Impl;

    private:
        std::unique_ptr<Impl> impl_;

    public:
        std::string path;
        std::string name;
        int argc;
        const char** argv;
        const std::string title;

    public:
        std::function<void()> onInit;

    public:
        application(int argc, const char** argv, const std::string& title);
        ~application();
        int loop();
        void exit(const int& exitcode) const;
        std::string datadir(const std::string& an) const;
    };

    const s::application& app();
} // s
