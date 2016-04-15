#pragma once
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

        /// \brief convert function parameter from string to native
        template <typename T>
        inline T convertFromJS(conversion_context& ctx, size_t& idx) {
            T val;
            std::istringstream ss(ctx.args.at(idx++));
            ss >> val;
            return val;
        }

        /// \brief specialise for bool
        template <>
        inline bool convertFromJS<bool>(conversion_context& ctx, size_t& idx) {
            auto& p = ctx.args.at(idx++);
            if(p == "true"){
                return true;
            }
            return false;
        }

        /// \brief specialise for string
        /// don't go thru stringstream
        template <>
        inline std::string convertFromJS<std::string>(conversion_context& ctx, size_t& idx) {
            return ctx.args.at(idx++);
        }

        /// \brief convert return value from native to string
        template <typename T>
        inline void setReturn(conversion_context& ctx, const T& t) {
            std::ostringstream ss;
            ss << t;
            ctx.retv = ss.str();
        }

        /// \brief specialise for bool
        template <>
        inline void setReturn<bool>(conversion_context& ctx, const bool& t) {
            if(t){
                ctx.retv = "true";
            }
            ctx.retv = "false";
        }

        /// \brief specialise for string
        template <>
        inline void setReturn<std::string>(conversion_context& ctx, const std::string& t) {
            ctx.retv = t;
        }

        /////////////////////////////////////////////////
        template <typename T>inline std::string getJsTypeName();
        template <>inline std::string getJsTypeName<std::string>(){return "string";}
        template <>inline std::string getJsTypeName<int>(){return "number";}
        template <>inline std::string getJsTypeName<bool>(){return "bool";}

        /////////////////////////////////////////////////
        static inline auto getAddParamStatement(const size_t& idx, const std::string& t, std::ostringstream& ss){
            std::ostringstream ts;
            ts << "p" << idx;
            auto var = ts.str();

            ss << "    if(typeof " << var << " == '" << t << "') {" << std::endl;
            if(t == "string"){
                ss << "      rv.push(" << var << ");" << std::endl;
            }else{
                ss << "      rv.push(String(" << var << "));" << std::endl;
            }
            ss << "    }else{" << std::endl;
            if(t == "string"){
                ss << "      rv.push(String(" << var << "));" << std::endl;
            }else{
                ss << "      nproxy.error('');" << std::endl;
            }
            ss << "    }" << std::endl;
            return var;
        }

        static inline void addStringList(std::ostringstream& /*ss*/, std::ostringstream& /*sp*/){
            assert(false);
        }

        template <typename T, typename... A>
        static inline void addStringList(std::ostringstream& ss, std::ostringstream& sp, T t, A... a){
            auto var = getAddParamStatement(sizeof...(A), t, ss);
            sp << var;
            if(sizeof...(A) > 0){
                sp << ", ";
                addStringList(ss, sp, a...);
            }
        }

        template <typename... A>
        static inline std::string getFunctionBody(const std::string& name){
            std::ostringstream ss;
            std::ostringstream sp;
            addStringList(ss, sp, getJsTypeName<typename std::decay<A>::type>()...);

            std::ostringstream os;
            os << "function(" << sp.str() << "){" << std::endl;
            os << "    var rv = new Array();" << std::endl;
            os << ss.str() << std::endl;
            os << "    var vv = this.__nobj__.invoke('" << name << "', rv);" << std::endl;
//            os << "    return valueOf(vv);" << std::endl;
            os << "    return vv;" << std::endl;
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
                auto rv = fnx(convertFromJS<typename std::decay<Args>::type>(ctx, idx)...);
                setReturn(ctx, rv);
            }
            static inline void obj_run(Cls obj, FnT fn, conversion_context& ctx) {
                size_t idx = 0;
                auto rv = (obj.*fn)(convertFromJS<typename std::decay<Args>::type>(ctx, idx)...);
                setReturn(ctx, rv);
            }
        };

        template <typename Cls, typename... Args>
        struct Invoker<void (Cls::*)(Args...)> {
            typedef void (Cls::*FnT)(Args...);
            static inline void afn_run(Cls fnx, conversion_context& ctx) {
                size_t idx = 0;
                fnx(convertFromJS<typename std::decay<Args>::type>(ctx, idx)...);
            }
            static inline void obj_run(Cls obj, FnT fn, conversion_context& ctx) {
                size_t idx = 0;
                (obj.*fn)(convertFromJS<typename std::decay<Args>::type>(ctx, idx)...);
            }
        };

        /////////////////////////////////////////////////
        template <typename T>
        struct class_def {};

        // member function
        template <typename Cls, typename Ret, typename... Args>
        struct class_def<Ret (Cls::*)(Args...)> {
            typedef Ret (Cls::*FnT)(Args...);
            static inline void afn_invoke(Cls f, conversion_context& ctx) {
                return Invoker<FnT>::afn_run(f, ctx);
            }
            static inline void obj_invoke(Cls obj, FnT fn, conversion_context& ctx) {
                return Invoker<FnT>::obj_run(obj, fn, ctx);
            }
            static inline auto stringy(const std::string& name) {
                return getFunctionBody<Args...>(name);
            }
        };

        // const member function
        template <typename Cls, typename Ret, typename... Args>
        struct class_def<Ret (Cls::*)(Args...) const> {
            typedef Ret (Cls::*FnT)(Args...);
            static inline void afn_invoke(Cls f, conversion_context& ctx) {
                return Invoker<FnT>::afn_run(f, ctx);
            }
            static inline void obj_invoke(Cls obj, FnT fn, conversion_context& ctx) {
                return Invoker<FnT>::obj_run(obj, fn, ctx);
            }
            static inline auto stringy(const std::string& name) {
                return getFunctionBody<Args...>(name);
            }
        };

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
                ss_ << "function " << name_ << "(nobj) {" << std::endl;
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
                    setReturn(ctx, rv);
                };

                auto sn = "set_" + name;
                fnl_[sn] = [p](s::js::conversion_context& ctx, ObjT& obj) {
                    size_t idx = 0; // need this coz idx parameter to convertFromJS() is a non-const ref
                    (obj.*p) = convertFromJS<PT>(ctx, idx);
                };

                std::ostringstream sp;
                auto var = getAddParamStatement(0, getJsTypeName<PT>(), sp);

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
        /// \brief cross-platform systray functions
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
                eval("javascript:" + kls.str());
            }

            /// \brief add native object into DOM
            void addNativeObject(const std::string& name, s::js::objectbase& jo);

            template <typename ObjT>
            inline void setObject(const s::js::klass<ObjT>& kls, const std::string& name, ObjT& obj) {
                objList_[name] = std::make_unique<s::js::objectT<ObjT>>(name, kls, obj);
                auto& jo = objList_[name];
                addNativeObject(jo->nname, *jo);
                eval("javascript:var " + name + " = new " + kls.name_ + "(" + jo->nname + ");");
            }

            inline auto& newObject(const std::string& name) {
                auto nobj = std::make_unique<s::js::object>(name);
                auto pobj = nobj.get();
                objList_[name] = std::move(nobj);
                auto& jo = objList_[name];
                addNativeObject(jo->nname, *jo);
                return *pobj;
            }

            inline void addObject(s::js::object& obj) {
                obj.kls.end();
                auto body = "var " + obj.name + " = new " + obj.kls.str() + "(" + obj.nname + ");\n";
                eval("javascript:" + body);
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
