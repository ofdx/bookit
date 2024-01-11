/*
   OFDX FCGI Common Code
   mperron (2024)
*/

#include "fcgi/fcgi.hpp"

#include <iostream>
#include <fstream>
#include <sstream>

#define PORT_OFDX_BOOKIT            9020
std::string const PATH_OFDX_BOOKIT("/bookit/");

struct OfdxBaseConfig {
	std::string m_addr;
	int m_port, m_backlog;

	std::string m_baseUriPath, m_dataPath;

	OfdxBaseConfig(int port, std::string const& baseUriPath) :
		m_addr("127.0.0.1"), m_port(port), m_backlog(64),

		m_baseUriPath(baseUriPath)
	{}

	virtual void receiveCliArgument(std::string const& k, std::string const& v) {}
	virtual void receiveCliOption(std::string const& opt) {}

	bool processCliArguments(int argc, char **argv){
		for(int i = 1; i < argc; ++ i){
			std::stringstream ss(argv[i]);
			std::string k, v;

			if(ss >> k){
				if(k == "port"){
					int vi;

					if(ss >> vi)
						m_port = vi;
				} else if(k == "backlog"){
					int vi;

					if(ss >> vi)
						m_backlog = vi;
				} else if(ss >> v){
					if(k == "addr"){
						m_addr.assign(v);
					} else if(k == "baseuri"){
						m_baseUriPath.assign(v);
					} else if(k == "datapath"){
						m_dataPath.assign(v);
					} else {
						receiveCliArgument(k, v);
					}
				} else {
					receiveCliOption(k);
				}
			}
		}

		return true;
	}
};

class OfdxFcgiService {
protected:
	std::shared_ptr<dmitigr::fcgi::Listener> m_pServer;
	std::unordered_map<std::string, std::string> m_cookies;

	virtual void handleConnection(std::unique_ptr<dmitigr::fcgi::Server_connection> const& conn) = 0;

	void parseCookies(std::unique_ptr<dmitigr::fcgi::Server_connection> const& conn){
		std::string session;

		// Erase anything that might be in here from a previous connection.
		m_cookies.clear();

		try {
			std::string http_cookie(conn->parameter("HTTP_COOKIE"));
			std::stringstream http_cookie_ss(http_cookie);
			std::string cookie;

			while(http_cookie_ss >> cookie){
				size_t n = cookie.find('=');

				for(auto & c : cookie){
					if(c == ';'){
						c = 0;
						break;
					}
				}

				if((n > 0) && (n < cookie.size() - 1)){
					m_cookies[cookie.substr(0, n).c_str()] = cookie.substr(n + 1).c_str();
				}
			}

		} catch(...){}
	}

	// Callback for template processing. If a document contains "<?ofdx example tpl here>" then text will contain " example tpl here".
	virtual void fillTemplate(std::unique_ptr<dmitigr::fcgi::Server_connection> const& conn, std::string const& text){}

	void parseTemplateLine(std::unique_ptr<dmitigr::fcgi::Server_connection> const& conn, std::string const& line){
		std::string const keystr("<?ofdx");

		// Look for template and if found, call the replacement function.
		auto a = line.find(keystr);

		if(a != std::string::npos){
			auto const b = line.find('>', a);

			if(b != std::string::npos){
				conn->out() << line.substr(0, a);

				a += keystr.length();

				std::string const text(line.substr(a, (b - a)));

				fillTemplate(conn, text);
				parseTemplateLine(conn, line.substr(b + 1));

				return;
			}
		}

		conn->out() << line << std::endl;
	}

	bool serveTemplatedDocument(std::unique_ptr<dmitigr::fcgi::Server_connection> const& conn, std::string fname, bool includeHtmlContentType = false){
		std::ifstream infile(fname);

		if(infile){
			std::string line;

			if(includeHtmlContentType)
				conn->out() << "Content-Type: text/html; charset=utf-8" << std::endl << std::endl;

			while(std::getline(infile, line))
				parseTemplateLine(conn, line);

			return true;
		}

		return false;
	}

public:
	void listen(OfdxBaseConfig const& cfg){
		if(!m_pServer){
			dmitigr::fcgi::Listener_options options {
				cfg.m_addr, cfg.m_port, cfg.m_backlog
			};

			m_pServer = std::make_shared<dmitigr::fcgi::Listener>(options);
			m_pServer->listen();
		}
	}

	bool accept(){
		try {
			if(const auto conn = m_pServer->accept())
				handleConnection(conn);

		} catch(std::exception const& e){
			std::cerr << "Error: " << e.what() << std::endl;
		}

		return true;
	}
};
