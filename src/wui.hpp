#pragma once
#include <sstream>
#include <map>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <assert.h>

namespace s {
    /// \brief web UI
    namespace wui {
        /// \brief supress warnings
        template <typename T>
        inline void unused(const T&){}

        /// \brief cross-platform systray functions
        /// \todo: to implement
        struct systray {
        };

        /// \brief cross-platform systray functions
        struct menu {
            std::string name;
            inline menu(const std::string& n)
            : name(n) {}
            inline menu& submenu(const std::string& n, std::function<void()> fn) {
                unused(n);
                unused(fn);
                return *this;
            }
        };

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

        /// \brief specialise for string
        template <>
        inline void setReturn<std::string>(conversion_context& ctx, const std::string& t) {
            ctx.retv = t;
        }

        /////////////////////////////////////////////////
        template <typename T>inline std::string getJsTypeName();
        template <>inline std::string getJsTypeName<std::string>(){return "string";}
        template <>inline std::string getJsTypeName<int>(){return "number";}

        /////////////////////////////////////////////////
        template <typename F>
        struct Invoker {};

        template <typename Cls, typename Res, typename... Args>
        struct Invoker<Res (Cls::*)(Args...)> {
            static inline void run(Cls fnx, conversion_context& ctx) {
                size_t idx = 0;
                auto rv = fnx(convertFromJS<typename std::decay<Args>::type>(ctx, idx)...);
                setReturn(ctx, rv);
            }
        };

        template <typename Cls, typename... Args>
        struct Invoker<void (Cls::*)(Args...)> {
            static inline void run(Cls fnx, conversion_context& ctx) {
                size_t idx = 0;
                fnx(convertFromJS<typename std::decay<Args>::type>(ctx, idx)...);
            }
        };

        /////////////////////////////////////////////////
        static inline void addStringList(std::ostringstream& /*ss*/, std::ostringstream& /*sp*/){
            assert(false);
        }

        template <typename T, typename... A>
        static inline void addStringList(std::ostringstream& ss, std::ostringstream& sp, T t, A... a){
            std::ostringstream ts;
            ts << "p" << sizeof...(A);
            auto var = ts.str();

            ss << "  if(typeof " << var << " == '" << t << "') {" << std::endl;
            if(t == "string"){
                ss << "      rv.push(" << var << ");" << std::endl;
            }else{
                ss << "      rv.push(String(" << var << "));" << std::endl;
            }
            ss << "  }else{" << std::endl;
            if(t == "string"){
                ss << "      rv.push(String(" << var << "));" << std::endl;
            }else{
                ss << "      nproxy.error('');" << std::endl;
            }
            ss << "  }" << std::endl;

            sp << var;
            if(sizeof...(A) > 0){
                sp << ", ";
                addStringList(ss, sp, a...);
            }
        }

        template <typename... A>
        static inline std::string getFunctionBody(const std::string& obname, const std::string& name){
            std::ostringstream ss;
            std::ostringstream sp;
            addStringList(ss, sp, getJsTypeName<typename std::decay<A>::type>()...);

            std::ostringstream os;
            os << "function(" << sp.str() << "){" << std::endl;
            os << "  var rv = new Array();" << std::endl;
            os << ss.str() << std::endl;
            os << "  return nproxy.invoke('" + obname + "', '" << name << "', rv);" << std::endl;
            os << "}" << std::endl;
            return os.str();
        }

        /////////////////////////////////////////////////
        template <typename T>
        struct class_def {};

        // member function
        template <typename Cls, typename Ret, typename... Args>
        struct class_def<Ret (Cls::*)(Args...)> {
            static inline void invoke(Cls f, conversion_context& ctx) {
                return Invoker<Ret (Cls::*)(Args...)>::run(f, ctx);
            }
            static inline auto stringy(const std::string& obname, const std::string& name) {
                return getFunctionBody<Args...>(obname, name);
            }
        };

        // const member function
        template <typename Cls, typename Ret, typename... Args>
        struct class_def<Ret (Cls::*)(Args...) const> {
            static inline void invoke(Cls f, conversion_context& ctx) {
                return Invoker<Ret (Cls::*)(Args...)>::run(f, ctx);
            }
            static inline auto stringy(const std::string& obname, const std::string& name) {
                return getFunctionBody<Args...>(obname, name);
            }
        };

        /////////////////////////////////////////////////
        template <typename T>
        struct get_signature_impl {
            using cdef = class_def<decltype(&std::remove_reference<T>::type::operator())>;
        };

        class window;
        /////////////////////////////////////////////////
        struct jobject {
            std::string name;
            s::wui::window& win;
            std::map<std::string, std::function<void(s::wui::conversion_context& ctx)>> fnl_;
            inline jobject(s::wui::window& w, const std::string& n) : win(w), name(n) {}
            // todo: this gets called before definition
            inline void addFunction(const std::string& fnname, const std::string& body, std::function<void(conversion_context& ctx)> fnsig);

            struct jfunction {
                jobject& obj;
                std::string name;

                /////////////////////////////////////////////////
                template <typename T>
                inline jfunction& operator=(T fnx) {
                    std::function<void(conversion_context & ctx)> invoke_fn = [fnx](conversion_context& ctx) {
                        get_signature_impl<T>::cdef::invoke(fnx, ctx);
                    };
                    auto body = get_signature_impl<T>::cdef::stringy(obj.name, name);
                    obj.addFunction(name, body, invoke_fn);
                    return *this;
                }

                inline jfunction& operator=(const jfunction&) = delete;

                inline jfunction(jobject& o, const std::string& n)
                    : obj(o)
                    , name(n) {}
            }; // jfunction

            inline jfunction fn(const std::string& n) {
                return jfunction(*this, n);
            }
        }; // jobject

        /// \brief page content location
        enum class ContentSourceType {
            Embedded, /// \brief content is embedded in the executable
            Resource, /// \brief content is in the OS defined resource location for the application
            Standard, /// \brief content is from net
        };

        class window {
        public:
            struct Impl;

        private:
            std::unique_ptr<Impl> impl_;
            std::map<std::string, std::unique_ptr<jobject>> objList_;

        public:
            std::function<void()> onOpen;
            std::function<void()> onClose;
            std::function<void(const std::string&)> onLoad;
            std::function<void(const std::string&)> onLog;

        public:
            void setContentSourceEmbedded(const std::map<std::string, std::tuple<const unsigned char*, size_t, std::string>>& lst);
            const std::tuple<const unsigned char*, size_t, std::string>& getEmbeddedSource(const std::string& url);
            bool open();
            void setMenu(const menu& m);

            window();
            ~window();

            void go(const std::string& url);

            inline std::string invoke(const std::string& obj, const std::string& fn, const std::vector<std::string>& params) {
                auto oit = objList_.find(obj);
                if (oit == objList_.end()) {
                    throw std::runtime_error(std::string("unknown object:") + obj);
                }
                auto fit = oit->second->fnl_.find(fn);
                if (fit == oit->second->fnl_.end()) {
                    throw std::runtime_error(std::string("unknown function:") + obj + "." + fn);
                }

                s::wui::conversion_context ctx(params);
                fit->second(ctx);
                return ctx.retv;
            }

            inline jobject& addObject(const std::string& name) {
                objList_[name] = std::make_unique<jobject>(*this, name);
                auto& obj = objList_[name];
                eval("javascript:(function(){" + name + " = {};}());");
                return *obj;
            }

            /// \brief eval a string
            /// should always be called on main thread
            void eval(const std::string& str);
        };

        inline void jobject::addFunction(const std::string& fnname, const std::string& body, std::function<void(conversion_context& ctx)> fnsig){
            win.eval("javascript:(function(){" + name + "." + fnname + " = " + body + ";}());");
            fnl_[fnname] = fnsig;
        }
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
