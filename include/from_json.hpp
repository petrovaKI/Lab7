// Copyright 2022 Petrova Kseniya <ksyushki5@yandex.ru>

#ifndef INCLUDE_FROM_JSON_HPP_
#define INCLUDE_FROM_JSON_HPP_

#include <iostream>
#include <string>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

class LoadFromFile{
 public:
  explicit LoadFromFile(const std::string path);
  json get_collection() const;
  void load_col_from_file();

 private:
  json _collection; //коллекция
  std::string _path; //путь до файла с коллекцией
};

#endif  // INCLUDE_FROM_JSON_HPP_
