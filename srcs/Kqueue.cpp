#include "Kqueue.hpp"

Kqueue::Kqueue()
: kq(-1), nchanges(0), nevents(0), change_list(NULL), event_list(NULL) {
	kcf.changes = 512;
	kcf.events = 512;
	ts.tv_sec = 5;
	ts.tv_nsec = 0;
}

Kqueue::~Kqueue()
{ kqueue_done(); }

void	Kqueue::kqueue_init() {
	if (kq == -1) {
		kq = kqueue();
		if (kq == -1) {
			Logger::log_error(LOG_EMERG, "kqueue() failed");
			throw kqueueException();
		}
	}

	change_list = new struct kevent[kcf.changes]();
	if (change_list == NULL) {
		Logger::log_error(LOG_EMERG, "malloc(%u) failed", (size_t)kcf.changes);
		throw std::bad_alloc();
	}
	event_list = new struct kevent[kcf.events]();
	if (event_list == NULL) {
		Logger::log_error(LOG_EMERG, "malloc(%u) failed", (size_t)kcf.events);
		throw std::bad_alloc();
	}
	max_changes = kcf.changes;
	nevents = kcf.events;
}

void	Kqueue::kqueue_done() {
	if (close(kq) == -1) {	// throw ??
		Logger::log_error(LOG_ALERT, "kqueue close() failed");
	}
	kq = -1;
	delete[] change_list;
	delete[] event_list;
	max_changes = 0;
	nchanges = 0;
	nevents = 0;
}

// Connection 하나 등록
void	Kqueue::kqueue_set_event(Connection *c, u_short filter, u_int flags)
{
	EV_SET(&change_list[nchanges], c->get_fd(), filter, flags, 0, 0, c);	// udata = Connection
	++nchanges;
}

void	Kqueue::kqueue_process_events(SocketManager *sm)
{
	int				events;
	Event_Handler	event_handler;
	size_t			recv_len;

	events = kevent(kq, change_list, nchanges, event_list, nevents, &ts);
	nchanges = 0;
	if (events == -1) {
		Logger::log_error(LOG_ALERT, "kevent() failed");
		throw keventException();
	}
	for (int_t i = 0; i < events; ++i) {
		Connection *c = (Connection*)event_list[i].udata;

		if (event_list[i].flags & EV_ERROR) {
			Logger::log_error(LOG_ALERT, "%d kevent() error on %d filter:%d", events, (int)event_list[i].ident, (int)event_list[i].filter);
			continue ;
		}
		else if (event_list[i].filter == EVFILT_READ) {
			if (c->get_listen()) {
				Connection *conn = c->event_accept(sm);	// throw
				kqueue_set_event(conn, EVFILT_READ, EV_ADD);
			}
			else if (event_list[i].flags & EV_EOF) {
				Logger::log_error(LOG_ALERT, "%d kevent() reported about an closed connection %d", events, (int)event_list[i].ident);
				sm->close_connection(c);
			}
			else {
				recv_len = recv(event_list[i].ident, c->buffer, BUF_SIZE, 0);
				// event_handler.set_request_message(c->buffer);
				// event_handler.test_print_origin_message();
				// kqueue_set_event(c, EVFILT_WRITE, EV_ADD | EV_ONESHOT);

				// buffer 이어붙이는 기능
				// if (event_handler.set_request_message(c->buffer))
				if (event_handler.append_buffer_to_request_message(c->buffer))
				{
					event_handler.parse_req_msg();
					// c->set_data(event_handler.get_req_msg());
					c->set_request_message(event_handler.get_req_msg());
					event_handler.test_print_origin_message();
					kqueue_set_event(c, EVFILT_WRITE, EV_ADD | EV_ONESHOT);
				}
        // test 를 위해서 write event filter 를 추가합니다.
        kqueue_set_event(c, EVFILT_WRITE, EV_ADD | EV_ONESHOT);
				memset(c->buffer, 0, recv_len);
			}
		}
		else if (event_list[i].filter == EVFILT_WRITE) {
			if (event_list[i].flags & EV_EOF) {
				Logger::log_error(LOG_ALERT, "%d kevent() reported about an %d reader disconnects", events, (int)event_list[i].ident);
				sm->close_connection(c);
			}
			else {
				// c->get_data();
				// generator response message
				// const HttpConfig	*get_httpconfig() const;
				std::string res_msg;
				// event_handler.process_event(res_msg, c->get_request_message(), c->get_httpconfig(), c->get_local_sockaddr());
				event_handler.process_event(res_msg, c->get_request_message(), c->get_local_sockaddr());
		std::cout << "===test print res_mes===" << std::endl;
		std::cout << res_msg << std::endl;
				// std::string res_msg = "HTTP/1.1 200 OK\r\nServer: jsnetwork\r\nContent-Length: 31\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE html>\n<html>\n</html>\n";
				send(event_list[i].ident, res_msg.c_str(), res_msg.size(), 0);
			}
		}
	}
}
