

#ifndef SERVER_HPP_
#define SERVER_HPP_

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <vector>
#include <fstream>
#include <iomanip>
#include <utility>

#include "from_json.hpp"
#include "suggestions.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
using nlohmann::json;

//выводим предложения
std::string output_response(const json& data) {
  std::stringstream ss;
  if (data.is_null())
    ss << std::endl << "{Suggestions: []}" << std::endl;
  else
    ss << std::endl << std::setw(1) << data << std::endl;
  return ss.str();
}
// HTTP-ответ для данного запроса.
// Тип объекта ответа зависит от содержимого запроса, поэтому интерфейс требует,
// чтобы вызывающий объект передал общую лямбду для получения ответа.
template <class Body, class Allocator, class Send>
void handle_request(http::request<Body, http::basic_fields<Allocator>>&& req,
                    Send&& send, const std::shared_ptr<std::timed_mutex>& mutex,
                    //объект класса Suggestion
                    const std::shared_ptr<Suggestions>& sorted_collection) {
  // Возвращает неверный ответ на запрос
  auto const bad_request = [&req](beast::string_view why) {
    http::response<http::string_body> res{http::status::bad_request,
                                          req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = std::string(why);
    res.prepare_payload();
    return res;
  };

  // Возвращает ненайденный ответ
  auto const not_found = [&req](beast::string_view target) {
    http::response<http::string_body> res{http::status::not_found,
                                          req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());
    res.body() = "The resource '" + std::string(target) + "' was not found.";
    res.prepare_payload();
    return res;
  };

  // Проверка на post-запрос
  if (req.method() != http::verb::post) {
    return send(bad_request("Unknown HTTP-method. You should use POST method"));
  }

  // Проверка на путь "/v1/api/suggest".
  if (req.target() != "/v1/api/suggest") {
    return send(not_found(req.target()));
  }

  //Парсим входные данные
  json input_body;
  try{
    input_body = json::parse(req.body());
  } catch (std::exception& e){
    return send(bad_request(e.what()));
  }
  //Проверяем входные данные
  boost::optional<std::string> input;
  try {
    input = input_body.at("input").get<std::string>();
  } catch (std::exception& e){
    return send(bad_request(R"(JSON format: {"input" : "<user_input">})"));
  }
  if (!input.has_value()){
    return send(bad_request(R"(JSON format: {"input" : "<user_input">})"));
  }
  mutex->lock();
  //Формируем из отсортированной коллекции предложения для пользователя
  auto result = sorted_collection->suggest_to_response(input.value());
  mutex->unlock();
  //Ответ
  http::string_body::value_type body = output_response(result);
  auto const size = body.size();

  http::response<http::string_body> res{
      std::piecewise_construct, std::make_tuple(std::move(body)),
      std::make_tuple(http::status::ok, req.version())};
  res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(http::field::content_type, "application/json");
  res.content_length(size);
  res.keep_alive(req.keep_alive());
  return send(std::move(res));
}
// Это эквивалент общей лямбда-выражения в C++11.
// Объект функции используется для отправки HTTP-сообщения.
template<class Stream>
struct send_lambda {
  Stream& stream_;
  bool& close_;
  beast::error_code& ec_;

  explicit send_lambda(Stream& stream, bool& close, beast::error_code& ec)
      : stream_(stream), close_(close), ec_(ec) {}

  template <bool isRequest, class Body, class Fields>
  void operator()(http::message<isRequest, Body, Fields>&& msg) const {
    // Определяем, следует ли закрывать соединение после.
    close_ = msg.need_eof();
    // Здесь нам нужен сериализатор, потому что сериализатор требует
    // неконстантного file_body, а ориентированная на сообщения версия
    // http::write работает только с константными сообщениями.
    http::serializer<isRequest, Body, Fields> sr{msg};
    http::write(stream_, sr, ec_);
  }
};
// Обрабатывает HTTP-соединение с сервером
void fail(beast::error_code ec, char const* what) {
  std::cerr << what << ": " << ec.message() << "\n";
}

void do_session(net::ip::tcp::socket& socket,
                const std::shared_ptr<Suggestions>& collection,
                const std::shared_ptr<std::timed_mutex>& mutex) {
  bool close = false;
  beast::error_code ec;
  beast::flat_buffer buffer;
  send_lambda<tcp::socket> lambda{socket, close, ec};

  for (;;) {
    http::request<http::string_body> req;
    http::read(socket, buffer, req, ec);
    if (ec == http::error::end_of_stream) break;
    if (ec) return fail(ec, "read");

    //обработка запроса
    handle_request(std::move(req), lambda, mutex, collection);
    if (ec) return fail(ec, "write");
    if (close) {
      // Это означает, что мы должны закрыть соединение, обычно потому,
      // что ответ указывает семантику «Соединение: закрыть».
      break;
    }
  }
  // Отправляем отключение TCP
  socket.shutdown(tcp::socket::shutdown_send, ec);
  // В этот момент соединение корректно закрыто.
}

//Кажждые 15 минут подгружаем данные в коллекцию
void suggestion_updater(
    const std::shared_ptr<LoadFromFile>& collection,
    const std::shared_ptr<Suggestions>& suggestions,
    const std::shared_ptr<std::timed_mutex>& mutex) {
  for (;;) {
    mutex->lock();
    //Загружаем коллекцию из файла
    collection->load_col_from_file();
    //Сортируем коллекцию
    suggestions->sort_response(collection->get_collection());
    mutex->unlock();
    std::cout << "-------------SERVER WAS UPDATED-------------"
              << std::endl;
    //засыпапем на 15 минут - обновление каждые 15 минут
    std::this_thread::sleep_for(std::chrono::minutes(15));
  }
}

#endif  // SERVER_HPP_
