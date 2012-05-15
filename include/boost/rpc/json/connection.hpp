#ifndef _BOOST_RPC_JSON_CONNECTION_HPP_
#define _BOOST_RPC_JSON_CONNECTION_HPP_
#include <boost/rpc/json/value_io.hpp>
#include <boost/rpc/error.hpp>
#include <boost/function.hpp>
#include <vector>
#include <boost/cmt/thread.hpp>
#include <boost/signals.hpp>
#include <boost/fusion/include/size.hpp>
#include <boost/fusion/include/front.hpp>
#include <boost/fusion/support/deduce.hpp>
#include <boost/fusion/include/make_fused_function_object.hpp>
#include <boost/mpl/if.hpp>
#include <boost/fusion/include/mpl.hpp>
#include <boost/fusion/adapted/mpl.hpp>
#include <boost/fusion/include/make_vector.hpp>
#include <boost/function_types/parameter_types.hpp>
#include <boost/function_types/result_type.hpp>
#include <boost/fusion/include/make_unfused.hpp>


namespace boost { namespace rpc { namespace json {
  class connection;
  typedef boost::function<json::value( connection&, const json::value& param )> rpc_method;

  struct named_parameters{};
  // namespace detail
  namespace detail {

     /**
      * If Seq size is 1 and it inherits from named_parameters then Seq will be
      * sent as named parameters.
      */
     template<typename Seq>
     struct has_named_params : 
        public boost::mpl::if_c<
                 (boost::is_base_of<named_parameters, 
                                    typename boost::fusion::traits::deduce<
                                        typename boost::fusion::result_of::front<Seq>::type >::type>::value 
                 && (1 ==  boost::fusion::result_of::size<Seq>::value))
        , boost::true_type, boost::false_type>::type 
     { };
  }

  /**
   *  Manages RPC call state including:
   *    - sending invokes, setting return codes, and handling promises
   *    - receiving invokes, calling methods, and sending return codes.
   *
   *  Does not implement communication details which are provided by derived classes
   *  which reimplement send() and call the  protected handler methods.
   */
  class connection : public boost::enable_shared_from_this<connection> {
    public:
      /**
       *  When packing parameters, intercept any boost function and
       *  create a callback for it.
       */
      struct function_filter {
         function_filter( connection& c ):m_con(c){}

         template<typename T>
         const T& operator()( const T& v ) { return v; }

         template<typename T>
         inline void operator()( const json::value& j, T& v ) { 
           json::io::unpack( *this, j, v ); 
         }
         template<typename Signature>
         inline void operator()( const json::value& j, boost::function<Signature> & v ) { 
             typedef typename boost::function_types::parameter_types<Signature>::type  mpl_param_types;
             typedef typename boost::fusion::result_of::as_vector<mpl_param_types>::type param_types;
             typedef typename boost::function_types::result_type<Signature>::type  R;

             cmt::future<R> (connection::*cf)(const std::string&, const param_types&) = &connection::call_fused;
             v = boost::fusion::make_unfused(boost::bind( cf, &m_con, (const std::string&)j, _1 ) );
           //return unpack( *this, j, v ); 
         }

         template<typename Signature>
         std::string operator()( const boost::function<Signature>& f ); 

         private:
             connection& m_con;
      };
      
      typedef boost::shared_ptr<connection> ptr;
      typedef boost::weak_ptr<connection>   wptr;

      /**
       *  @param t - the thread in which messages will be sent and callbacks invoked
       */
      connection( cmt::thread* t = &cmt::thread::current()  );
      ~connection();

      cmt::thread* get_thread()const;

      void add_method( const std::string& mid, const rpc_method& m );

      /** The connection will generate and return a method name **/
      std::string add_method( const rpc_method& m );


      #include <boost/rpc/json/detail/call_methods.hpp>


      template<typename ParamSeq>
      boost::cmt::future<json::value> call_fused( const std::string& method_name, const ParamSeq& param ) {
        json::value msg;
        msg["method"] = method_name;
        msg["id"]     = next_method_id();
        // TODO: transform functor params...

        if( boost::fusion::size(param ) )
          pack_params( msg["params"], param, typename detail::has_named_params<ParamSeq>::type() );

        typename pending_result_impl<json::value>::ptr pr = boost::make_shared<pending_result_impl<json::value> >(); 

        msg["jsonrpc"] = "2.0";
        send( msg, boost::static_pointer_cast<pending_result>(pr) );
        return pr->prom;
      }


      template<typename R, typename ParamSeq>
      boost::cmt::future<R> call_fused( const std::string& method_name, const ParamSeq& param ) {
        json::value msg;
        msg["method"] = method_name;
        msg["id"]     = next_method_id();
        // TODO: transform functor params...

        if( boost::fusion::size(param ) )
          pack_params( msg["params"], param, typename detail::has_named_params<ParamSeq>::type() );

        typename pending_result_impl<R>::ptr pr = boost::make_shared<pending_result_impl<R> >(); 

        msg["jsonrpc"] = "2.0";
        send( msg, boost::static_pointer_cast<pending_result>(pr) );
        return pr->prom;
      }
      template<typename ParamSeq>
      void notice_fused( const std::string& method_name, const ParamSeq& param ) {
        json::value msg;
        msg["method"] = method_name;
        // TODO: JSON RCP 1.0 sets this to 'null' instead of being empty
        //msg["id"]     = next_method_id();

        // TODO: transform functor params...
         
        // TODO: JSON RPC 1.0 does not allow empty param
        if( boost::fusion::size(param ) )
          pack_params( msg["params"], param, typename detail::has_named_params<ParamSeq>::type() );

        msg["jsonrpc"] = "2.0";
        send( msg );
      }

      boost::signal<void()> closed;

    protected:
     // change how params are packed based upon whether or not they are named params
     template<typename Seq>
     void pack_params( json::value& v, const Seq& s, const boost::true_type& is_named ) {
        function_filter f(*this);
        json::io::pack(f, v, boost::fusion::at_c<0>(s));
     }
     template<typename Seq>
     void pack_params( json::value& v, const Seq& s, const boost::false_type& is_named ) {
        function_filter f(*this);
        json::io::pack(f, v,s);
     }


      virtual void send( const json::value& msg ) = 0;

      void break_promises();
      void handle_notice( const json::value& m );
      void handle_call(   const json::value& m );
      void handle_result( const json::value& m );
      void handle_error(  const json::value& m );

      class pending_result {
        public:
          typedef boost::shared_ptr<pending_result> ptr;
          virtual ~pending_result(){}
          virtual void handle_result( connection& c, const json::value& data )       = 0;
          virtual void handle_error( const boost::exception_ptr& e  ) = 0;
      };

    private:
      friend class connection_private;

      uint64_t next_method_id();

      void send( const json::value& msg, const connection::pending_result::ptr& pr );


      template<typename R> 
      class pending_result_impl : public pending_result {
        public:
          pending_result_impl():prom(new boost::cmt::promise<R>()){}
          ~pending_result_impl() {
            if( !prom->ready() ) {
              prom->set_exception( boost::copy_exception( boost::cmt::error::broken_promise() ));
            }
          }
          typedef boost::shared_ptr<pending_result_impl> ptr;
          virtual void handle_result( connection& c, const json::value& data ) {
            R value;
            function_filter f(c);
            json::io::unpack( f, data, value );
            prom->set_value( value );
          }
          virtual void handle_error( const boost::exception_ptr& e  ) {
            prom->set_exception(e);
          }
          typename boost::cmt::promise<R>::ptr prom;
      };
      class connection_private* my;
  };

  namespace detail {

     template<typename Seq, typename Functor,bool NamedParams>
     struct rpc_recv_functor {
       rpc_recv_functor( Functor f )
       :m_func(f){ }

       json::value operator()( json::connection& c, const json::value& param ) {
         Seq paramv;
         if( boost::fusion::size(paramv) ) {
            if( !param.is_array() ) {
              BOOST_RPC_THROW( "param value is not an array" );
            }
            connection::function_filter f(c);
            json::io::unpack( f, param, paramv );
         }
         json::value rtn;
         connection::function_filter f(c);
         json::io::pack( f, rtn, m_func(paramv) );
         return rtn;
       }

       Functor m_func;
     };

     /**
      * 
      */
     template<typename Seq, typename Functor>
     struct rpc_recv_functor<Seq,Functor,true> {
       rpc_recv_functor( Functor f )
       :m_func(f){  }

       json::value operator()( json::connection& c, const json::value& param ) {
         Seq paramv;
         if( param.is_array() ) {
            json::io::unpack( param, paramv );
         }
         else if( param.is_object() ) {
            connection::function_filter f(c);
            json::io::unpack( f, param, boost::fusion::at_c<0>(paramv) );
         } else {
            BOOST_RPC_THROW( "param value is not an object or array" );
         }
         json::value rtn;
         connection::function_filter f(c);
         json::io::pack( f, rtn, m_func(paramv) );
         return rtn;
       }

       Functor m_func;
     };
  }


  template<typename Signature>
  std::string connection::function_filter::operator()( const boost::function<Signature>& f ) {
     typedef typename boost::function_types::parameter_types<Signature>::type  mpl_param_types;
     typedef typename boost::fusion::result_of::as_vector<mpl_param_types>::type param_types;
     typedef typename boost::function_types::result_type<Signature>::type  R;
     return m_con.add_method( detail::rpc_recv_functor<param_types,boost::function<R(param_types)>,false>( boost::fusion::make_fused_function_object(f) ) );
  }

} } } // boost::rpc::json

#endif
