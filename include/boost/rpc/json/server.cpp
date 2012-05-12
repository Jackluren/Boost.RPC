#include <boost/rpc/json/detal/tcp_server.hpp>

namespace boost { namespace rpc { namespace json {

  namespace detail {

    class server_base_private {
      typedef cmt::asio::tcp::socket socket_t;

      cmt::thread*                                       _thread;
      tcp_server_base::session_creator*                  _sc;
      boost::future<void>                                _listen_done;
      uint16_t                                           _port;
      boost::shared_ptr<boost::asio::ip::tcp::acceptor>  _acc;
      std::map<json::tcp_connection::ptr,boost::any>    _connections;

      void on_connection( const json::tcp_connection::ptr& c ) {
        c->closed.connect( boost::bind( &server_base_private::on_disconnect, this, c ) );
        _connections[c] = _sc->init_connection(c);
      }
      void on_disconnect( const json::tcp_connection::ptr& c ) {
        _connectaions.erase( c );
      }

      void listen(  ) {
        try {
          _acc = boost::make_shared<boost::asio::ip::tcp::acceptor>( 
                    cmt::asio::default_io_service(), 
                    boost::asio::ip::tcp::endpoint( boost::asio::ip::tcp::v4(),_port) );
         
          boost::system::error_code ec;
          do {
              socket_t::ptr iosp(new socket_t());
              ec = cmt::asio::tcp::accept( *_acc, *iosp);
              if(!ec) {
                  cmt::async( boost::bind(handle, 
                              json::tcp::connection::ptr( new json::tcp::connection(iosp) ) )); 
              } else { 
                  elog( "%1%", boost::system::system_error(ec).what() );
              }
          }while( !ec );
          //TODO:  signal done... 
          //cmt::async(boost::bind(handle,connection::ptr()));


        } catch ( const boost::exception& e ) {
           elog( "%1%", boost::diagnostic_information(e) );
           // TODO: signal done... 
        }
      }
    };

    tcp_server_base::tcp_server_base( tcp_server_base::session_creator* sc, uint16_t port, cmt::thread* t ) {
      my = new tcp_server_base_private;
      my->_sc     = sc;
      my->_thread = t;
      my->_port   = port;

      my->listen_done = t->cmt::async<void>( boost::bind( &server_base_private::listen, my ) );
    }


    tcp_server_base::~tcp_server_base() {
      try {
         my->_acc->close();
         my->_listen_done.wait();
      } catch ( const boost::exception& e ) {
         elog( "%1%", boost::diagnostic_information(e) );
      }
      delete my;
    }

  } // namespace detail

} } } // namespace boost::rpc::json

class server_private {
  public:
      typedef std::list< std::pair<rpc::connection::ptr,boost::any> > con_list;
};