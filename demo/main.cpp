// Copyright 2021 Petrova Kseniya <ksyushki5@yandex.ru>
#include <boost/program_options.hpp>
#include "server.hpp"

namespace po = boost::program_options;

int main(int argc, char *argv[]) {

  std::shared_ptr<std::timed_mutex> mutex =
      std::make_shared<std::timed_mutex>();
  std::shared_ptr<LoadFromFile> storage =
      std::make_shared<LoadFromFile>
          ("/home/kseniya/lab-07-http-server/suggestions.json");
  std::shared_ptr<Suggestions> suggestions =
      std::make_shared<Suggestions>();

  // Проверяем аргументы командной строки.
  try {
    if (argc != 3) {
      std::cerr << "Usage: suggestion_server <address> <port> \n"
                << "Example:\n"
                << "     127.0.0.1 80\n";
      return EXIT_FAILURE;
    }
    //IP
    auto const address = net::ip::make_address(argv[1]);
    //Порт
    auto const port = static_cast<uint16_t>(std::atoi(argv[2]));



    // контекст ввода-вывода
    net::io_context ioc{1};
    // Передаём адрес и порт сервера
    tcp::acceptor acceptor {ioc, {address, port}};
    //В потоке обновляем коллекцию и формируем предложения для пользователя
    std::thread{suggestion_updater, storage, suggestions, mutex}.detach();
    for (;;) {
      //новое соединение
      tcp::socket socket{ioc};
      // Блокируем, пока не получим соединение
      acceptor.accept(socket);
      // Запускаем сессию, передавая владение сокетом
      std::thread{std::bind(
                      &do_session,
                      std::move(socket),
                      suggestions,
                      mutex)}.detach();
    }
  } catch (std::exception& e) {

    std::cerr << "Error: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
}