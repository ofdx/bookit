/*
   BookIt
   mperron (2024)

   FastCGI service providing a web-based interface for reserving devices in a
   shared lab environment.
*/

#include "base64.h"
#include "ofdx_fcgi.h"
#include "res.h"
#include "ncsa.h"

#include <map>
#include <list>
#include <set>

#define BOOKIT_SID "bookit_sid"
#define OPEN_SID "open"
#define CLAIMED "Claimed"

void get_the_rest(std::stringstream &src, std::string &dest){
	if(src >> dest){
		std::string buf;

		if(getline(src, buf))
			dest += buf;
	}
}

struct Bookable {
	struct Reservation {
		std::string m_sessionId, m_info;
		time_t m_start, m_end;

		void debug(std::stringstream &ss){
			ss << "m_sessionId[" << m_sessionId << "] m_info[" << m_info << "] "
				<< "m_start[" << m_start << "] m_end[" << m_end << "] delta[" << m_end - m_start << "]\n";
		}

		Reservation() :
			m_start(0),
			m_end(0)
		{}
	};

	std::string m_id, m_name, m_desc, m_group;
	std::list<std::shared_ptr<Reservation>> m_reservations;

	static bool cmp(std::shared_ptr<Bookable::Reservation> const& a, std::shared_ptr<Bookable::Reservation> const& b){
		return (a->m_start < b->m_start);
	}
	void sortReservations(){
		m_reservations.sort(cmp);
	}
};

class OfdxBookIt : public OfdxFcgiService {
	time_t m_timenow;

	// Map by ID of everything we can book.
	std::map<std::string, std::shared_ptr<Bookable>> m_objects;
	std::map<std::string, std::shared_ptr<std::list<std::shared_ptr<Bookable>>>> m_objectsByGroup;

	void parseObject(std::ifstream &infile){
		std::shared_ptr<Bookable> b;
		std::string line;

		bool inObject = false;

		while(getline(infile, line)){
			// Allow for comments
			if(line[0] == '#')
				continue;

			// Skip empty lines before the start of the data.
			if(!b){
				if(line.empty())
					continue;

				b = std::make_shared<Bookable>();
			}

			if(line.empty()){
				// Get the body
				while(getline(infile, line)){
					if(line == ".")
						break;

					b->m_desc += line + "\n";
				}

				// Default group name.
				if(b->m_group.empty())
					b->m_group = "Other";

				// Object is complete, add to map.
				m_objects[b->m_id] = b;

				// Add to list ordered by group.
				auto l = m_objectsByGroup[b->m_group];
				if(!l){
					l = std::make_shared<std::list<std::shared_ptr<Bookable>>>();
					m_objectsByGroup[b->m_group] = l;
				}
				l->push_back(b);

				return;
			} else {
				// Parse the headers
				std::stringstream ss(line);
				std::string k;

				if(ss >> k){
					if(k == "id"){
						ss >> b->m_id;
					} else if(k == "name"){
						get_the_rest(ss, b->m_name);
					} else if(k == "group"){
						get_the_rest(ss, b->m_group);
					}
				}
			}
		}
	}

	void parseReservation(std::string const& line){
		std::stringstream ss(line);
		std::string id;
		std::shared_ptr<Bookable::Reservation> r = std::make_shared<Bookable::Reservation>();

		//cluster9 1704479574 1704483174 abcd1234b64 mperron
		if(ss >> id >> r->m_start >> r->m_end >> r->m_sessionId){
			// Unknown object?
			if(!m_objects.count(id))
				return;

			get_the_rest(ss, r->m_info);

			// Add reservation to object.
			{
				std::shared_ptr<Bookable> b = m_objects[id];
				b->m_reservations.push_back(r);
			}
		}
	}

public:
	struct OfdxBaseConfig m_cfg;

	std::string m_sessionId;

	OfdxBookIt() :
		m_cfg(PORT_OFDX_BOOKIT, PATH_OFDX_BOOKIT)
	{}

	bool processCliArguments(int argc, char **argv){
		if(m_cfg.processCliArguments(argc, argv)){
			// Database path must be set...
			if(m_cfg.m_dataPath.empty()){
				std::cerr << "Error: datapath must be set";
				return false;
			}
		}

		return true;
	}

	void loadObjects(){
		std::ifstream infile(m_cfg.m_dataPath + "objects.txt");

		while(infile)
			parseObject(infile);
	}

	void loadReservations(){
		std::ifstream infile(m_cfg.m_dataPath + "reservations.txt");
		std::string line;

		while(getline(infile, line)){
			if(!line.empty())
				parseReservation(line);
		}
	}

	void saveReservations(){
		std::ofstream outfile(m_cfg.m_dataPath + "reservations.txt");

		for(auto const& kv : m_objects){
			kv.second->sortReservations();

			// Find reservations we need to delete.
			{
				std::set<std::shared_ptr<Bookable::Reservation>> to_delete;

				for(auto it = kv.second->m_reservations.begin(); it != kv.second->m_reservations.end();){
					if((*it)->m_end < m_timenow){
						// Reservation is historical, delete it.
						it = kv.second->m_reservations.erase(it);
					} else if((*it)->m_sessionId == OPEN_SID){
						// Reservation is unclaimed space.
						to_delete.insert(*it);
					} else {
						// Unclaimed space must be preserved because an active
						// reservation exists after it.
						to_delete.clear();
					}

					++ it;
				}

				if(!to_delete.empty()){
					// Delete empty space.
					for(auto it = kv.second->m_reservations.begin(); it != kv.second->m_reservations.end();){
						if(to_delete.count(*it)){
							it = kv.second->m_reservations.erase(it);
						} else {
							++ it;
						}
					}
				}
			}

			// Save to disk.
			for(auto const& el : kv.second->m_reservations){
				//cluster9 1704479574 1704483174 abcd1234b64 mperron
				outfile
					<< kv.first << " "
					<< el->m_start << " "
					<< el->m_end << " "
					<< el->m_sessionId << " "
					<< el->m_info << "\n";
			}

		}

		outfile << std::endl;
	}

	void sendBadRequest(std::unique_ptr<dmitigr::fcgi::Server_connection> const& conn) const {
		conn->out()
			<< "Status: 400 Bad Request\r\n"
			<< "Content-Type: text/plain; charset=utf-8\r\n"
			<< "\r\n"
			<< "Bad request. You goofed!" << std::endl;
	}

	void manageSessionId(std::unique_ptr<dmitigr::fcgi::Server_connection> const& conn){
		m_sessionId = m_cookies[BOOKIT_SID];

		// Validate session ID string.
		for(char const& c : m_sessionId){
			if(!(
				((c >= 'a') && (c <= 'z')) ||
				((c >= 'A') && (c <= 'Z')) ||
				((c >= '0') && (c <= '9')) ||
				(c == '-') ||
				(c == '_') ||
				(c == '.')
			)){
				m_sessionId = "";
				break;
			}
		}

		// If the user does not have a session ID, assign one at random.
		if(m_sessionId.empty()){
			std::ifstream infile("/dev/urandom");

			if(infile){
				size_t len = 64;
				char *dbuf = (char*) calloc(len, sizeof(char*));

				// Read some random data.
				infile.read(dbuf, len);

				// Store as base64.
				m_sessionId = base64_encode(std::string(dbuf), len);
			}
		}

		// Set or refresh the user's cookie.
		conn->out()
			<< "Set-Cookie: " BOOKIT_SID "=" << m_sessionId
			<< "; SameSite=Strict; Path=/; Max-Age=" << (6 * 7 * 24 * 60 * 60) << "\r\n";
	}

	void sendHomePage(std::unique_ptr<dmitigr::fcgi::Server_connection> const& conn){
		conn->out()
			<< "Content-Type: text/html; charset=utf-8\r\n"
			<< "\r\n"
			<< resources["header.html"]
			<< "<p>Select a cluster from the list below to reserve it.</p>\n";

		for(auto const& kv : m_objectsByGroup){
			conn->out() << "<div class=clustergroup><h3>" << kv.first << "</h3>\n<ul>\n";

			for(auto const& el : *kv.second){
				time_t reserveduntil = 0;
				bool reservedbyyou = false;

				if(el->m_reservations.size()){
					for(auto const& r : el->m_reservations){
						if(r->m_end > m_timenow){
							if((r->m_start <= m_timenow) && (r->m_sessionId == m_sessionId)){
								reserveduntil = r->m_end;
								reservedbyyou = true;
								break;
							} else if((r->m_end > reserveduntil) && (r->m_sessionId != OPEN_SID)){
								reserveduntil = r->m_end;
							}
						}
					}
				}

				conn->out() << " <li><a ";

				if(reservedbyyou){
					conn->out() << "class=confirmed ";
				} else if(reserveduntil){
					conn->out() << "class=reserved ";
				}

				conn->out()
					<< "href=\"" << PATH_OFDX_BOOKIT << el->m_id << "\">"
					<< (reservedbyyou ? "*" : "") << el->m_name << "</a>";

				if(reserveduntil){
					conn->out() << " &mdash; <span class=\"utctime\">" << reserveduntil << "</span>";
				}

				conn->out() << "</li>\n";
			}

			conn->out() << "</ul>\n</div>\n";
		}

		conn->out()
			<< "<p>Clusters shown in <span class=reserved>red</span> are reserved until the time shown. "
			<< "Click on them for more details and to reserve at a future time.</p>"
			<< "<p>Clusters shown in <span class=confirmed>*green</span> are reserved by you until the time shown.</p>"
			<< "<span id=clock class=utctime>" << m_timenow << "</span>";

		conn->out() << resources["footer.html"] << std::endl;
	}

	void sendCreatePage(std::unique_ptr<dmitigr::fcgi::Server_connection> const& conn, std::shared_ptr<Bookable> const& b){
		conn->out()
			<< "Content-Type: text/html; charset=utf-8\r\n"
			<< "\r\n"
			<< resources["header.html"];

		conn->out() << "<h3>" << b->m_name << " (" << b->m_group << ")</h3>\n";
		if(!b->m_desc.empty()){
			conn->out() << "<pre>" << b->m_desc << "</pre>\n";
		}

		// Check for a claim or cancelation in the query string.
		time_t tocancel = 0, toclaim = 0;
		{
			// FIXME - there is a bug somewhere near here related to string handling.
			std::string const f_cancel("cancel=");
			std::string const f_claim("claim=");
			std::string q(conn->parameter("QUERY_STRING"));

			for(auto &c : q){
				if(c == '&')
					c = ' ';
			}

			std::stringstream qss(q);
			std::string kv;

			while(qss >> kv){
				if(kv.find(f_cancel) == 0){
					tocancel = atoi(kv.substr(f_cancel.size()).c_str());
				} else if(kv.find(f_claim) == 0){
					toclaim = atoi(kv.substr(f_claim.size()).c_str());
				}
			}
		}

		bool needPersist = false;
		bool willExtend = false;
		for(auto const& el : b->m_reservations){

			// Is this reservation historical?
			if(el->m_end < m_timenow)
				continue;

			// Cancel a reservation
			if((el->m_start == tocancel) && (el->m_sessionId == m_sessionId)){
				// Clear sessionid and info to indicate that nobody owns this time.
				el->m_sessionId = OPEN_SID;
				el->m_info = "";
				needPersist = true;
			}

			// Claim this session for us.
			if((el->m_start == toclaim) && (el->m_sessionId == OPEN_SID)){
				el->m_sessionId = m_sessionId;
				el->m_info = CLAIMED;
				needPersist = true;
			}

		}

		if(needPersist)
			saveReservations();

		std::shared_ptr<Bookable::Reservation> latest = nullptr;
		for(auto const& el : b->m_reservations){
			bool isOpen = (el->m_sessionId == OPEN_SID);
			bool isYours = (el->m_sessionId == m_sessionId);

			conn->out() << "<p><span class=";

			if(isYours){
				conn->out() << "confirmed>*Reserved</span> (by you) ";
			} else if(isOpen){
				conn->out() << "confirmed>Available</span> ";
			} else {
				conn->out() << "reserved>Reserved</span> ";
			}
			
			if(el->m_start > m_timenow)
				conn->out() << "from <span class=utctime>" << el->m_start << "</span> ";

			conn->out() << "until <span class=utctime>" << el->m_end << "</span>" << (el->m_info.empty() ? "" : " &mdash; ") << el->m_info;

			if(isYours){
				conn->out() << " <a class=cancelres href=\"?cancel=" << el->m_start << "\">Cancel</a>";
			} else if(isOpen){
				conn->out() << " <a class=cancelres href=\"?claim=" << el->m_start << "\">Claim</a>";
			}

			conn->out() << "</p>\n";

			if(!latest || el->m_end > latest->m_end){
				willExtend = isYours;
				latest = el;
			}
		}

		conn->out() << "<br>"
			<< "<button duration=60>1 hour</button>"
			<< "<button duration=120>2 hours</button>"
			<< "<button duration=240>4 hours</button>"
			<< "<button duration=360>6 hours</button>"
			<< "<button duration=480>8 hours</button>";

		conn->out() << "<form id=reserver method=POST>"
			<< "<input id=m_id type=hidden name=m_id value=\"" << b->m_id << "\">"

			<< "<label for=m_duration>Duration:</label><br>"
			<< "<input id=m_duration name=m_duration value=240> (minutes)<br>"


			<< "<label for=m_info>Your name or email alias:</label><br>"
			<< "<input id=m_info name=m_info><br>"

			<< "<input id=reserverform_submit type=submit value=\"BookIt!\">"
			<< "</form>\n";

		if(latest){
			if(willExtend){
				// You have the cluster reserved and can extend your time.
				conn->out() << "<p>You have this cluster reserved for <span class=confirmed>" << ((latest->m_end - m_timenow) / 60) << " more minutes</span>. "
					<< "Booking time will extend your reservation.</p>\n";
			} else {
				// Somebody else has the cluster reserved.
				conn->out() << "<p>Your reservation will start <span class=reserved>" << ((latest->m_end - m_timenow) / 60) << " minutes from now</span>, after <span class=utctime>" << latest->m_end << "</span>.</p>\n";
			}
		} else {
			// No active reservation.
			conn->out() << "<p>This cluster is <b>free</b>. Your reservation will start immediately.</p>\n";
		}

		conn->out()
			<< "<p>&nbsp;</p><p><a href=\"" << PATH_OFDX_BOOKIT << "\">Return</a> to main page.</p>"
			<< "<span id=clock class=utctime>" << m_timenow << "</span>"
			<< resources["footer.html"] << std::endl;
	}

	void sendReservedPage(std::unique_ptr<dmitigr::fcgi::Server_connection> const& conn, std::shared_ptr<Bookable> const& b){
		conn->out() << "Content-Type: text/html; charset=utf-8\r\n";

		std::shared_ptr<Bookable::Reservation> r_new = std::make_shared<Bookable::Reservation>();
		r_new->m_sessionId = m_sessionId;

		// Read POST data, perform reservation.
		int code = 200;
		std::string message;
		{
			std::string line;

			if(!getline(conn->in(), line)){
				// No data in body.
				code = 400;
				message = "Your browser sent an invalid request. Please try again.";
			} else {
				std::stringstream iss;

				// Split on ampersands
				for(auto &c : line)
					if((c == '&') || (c == ';'))
						c = ' ';

				iss.str(line);

				std::string const
					f_duration("m_duration="),
					f_info("m_info=");

				time_t duration = 0;

				while(iss >> line){
					if(line.find(f_duration) == 0){
						duration = atoi(line.substr(f_duration.size()).c_str());

						// Duration should be at least 15 minutes and no more than 24 hours.
						if((duration < 15) || (duration > (60 * 24))){
							// Clear the invalid value.
							duration = 0;
						}
					} else if(line.find(f_info) == 0){
						line = line.substr(f_info.size());
						unescape_url_param(line);
						r_new->m_info = line;
					}
				}

				if(r_new->m_info.empty()){
					code = 400;
					message = "Please provide your name or email alias.";
				} else if(!duration){
					code = 400;
					message = "Please reserve for at least 15 minutes, and no more than 1440 minutes (24 hours).";
				}

				// Actually perform the reservation.
				if(code == 200){
					std::shared_ptr<Bookable::Reservation> r_latest;

					// Find the latest reservation.
					for(auto const& r : b->m_reservations){
						// If the reservation ends in the future, and it ends at the furthest future date we have seen...
						if((r->m_end > m_timenow) && (!r_latest || (r->m_end > r_latest->m_end)))
							r_latest = r;
					}

					if(!r_latest){
						// If not reserved, create reservation starting now.
						r_new->m_start = m_timenow;
						r_new->m_end = (r_new->m_start + (duration * 60));

						b->m_reservations.push_back(r_new);

					} else if(r_latest->m_sessionId == m_sessionId){
						// If reserved and we own it, extend by duration.
						r_latest->m_end += (duration * 60);
						r_latest->m_info = r_new->m_info;
						r_new = r_latest;
					} else {
						// If reserved and we don't own it, set start time to end time + 1.
						r_new->m_start = r_latest->m_end + 1;
						r_new->m_end = (r_new->m_start + (duration * 60));

						b->m_reservations.push_back(r_new);
					}

					saveReservations();
				}
			}
		}

		conn->out()
			<< "Status: " << code << "\r\n"
			<< "\r\n"
			<< resources["header.html"];

		// Display success or error page.
		switch(code){
			case 400:
				conn->out() << "<p>" << message << "</p>\n";
				break;

			default:
				// Success, display reservation/cluster info.
				conn->out()
					<< "<p>Your reservation for <a href=\"" << PATH_OFDX_BOOKIT << b->m_id << "\">" << b->m_name << "</a> "
					<< "is <span class=confirmed>confirmed</span>: "
					<< "<span class=utctime>" << ((r_new->m_start == m_timenow) ? std::string("now") : std::to_string(r_new->m_start)) << "</span> &mdash; "
					<< "<span class=utctime>" << r_new->m_end << "</span></p>\n";

				if(r_new->m_info != CLAIMED)
					conn->out() << "<p>Thanks " << r_new->m_info << "!</p>\n";

				if(!b->m_desc.empty()){
					conn->out() << "<pre>" << b->m_desc << "</pre>\n";
				}
		}

		conn->out()
			<< "<p><a href=\"" << PATH_OFDX_BOOKIT << "\">Return</a> to main page.</p>"
			<< "<span id=clock class=utctime>" << m_timenow << "</span>"
			<< resources["footer.html"] << std::endl;
	}

	void handleConnection(std::unique_ptr<dmitigr::fcgi::Server_connection> const& conn) override {
		std::string const SCRIPT_NAME(conn->parameter("SCRIPT_NAME"));
		time(&m_timenow);

		parseCookies(conn);
		manageSessionId(conn);

		if(SCRIPT_NAME == PATH_OFDX_BOOKIT){
			sendHomePage(conn);
		} else if(SCRIPT_NAME.find(PATH_OFDX_BOOKIT) == 0){
			// Managing a cluster... which one?
			std::string clusterId(SCRIPT_NAME.substr(PATH_OFDX_BOOKIT.size()));

			if(m_objects.count(clusterId)){
				auto b = m_objects[clusterId];

				// Cluster exists
				if(conn->parameter("REQUEST_METHOD") == std::string("POST")){
					// Create the reservation and show the success page.
					sendReservedPage(conn, b);
				} else {
					// Show the create reservation page.
					sendCreatePage(conn, b);
				}
			} else {
				// Malformed URL or a bad cluster ID.
				conn->out()
					<< "Status: 404 Not Found\r\n"
					<< "Content-Type: text/html; charset=utf-8\r\n"
					<< "\r\n"
					<< "<!doctype html><html><head><title>Not Found - BookIt!</title></head><body>"
					<< "<p>The requested cluster was not found. <a href=\"" << PATH_OFDX_BOOKIT << "\">Return to reservation page</a>.</p>"
					<< std::endl;
			}
		} else {
			// Generic not found.
			conn->out()
				<< "Status: 404 Not Found\r\n"
				<< "Content-Type: text/plain; charset=utf-8\r\n"
				<< "\r\n"
				<< "The page you are looking for is missing."
				<< std::endl;
		}
	}
};

int main(int argc, char **argv){
	OfdxBookIt app;

	// Get config overrides from the CLI arguments.
	if(!app.processCliArguments(argc, argv))
		return 1;

	loadResources();

	// Decode base64 encoded files.
	for(auto & kv : resources)
		kv.second = base64_decode(kv.second);

	app.loadObjects();
	app.loadReservations();

	app.listen(app.m_cfg);

	while(app.accept());

	return 0;
}
