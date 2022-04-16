// Copyright 2022 Petrova Kseniya <ksyushki5@yandex.ru>

#include "suggestions.hpp"

Suggestions::Suggestions() {
  sorted_collection = {};
}
//Сортируем коллекцию по полю cost в порядке убывания
void Suggestions::sort_response(json collection) {
  std::sort(collection.begin(), collection.end(),
            [](const json& a, const json& b) -> bool {
              return a.at("cost") < b.at("cost");
            });
  //Создаём объект с отсортированной коллекцией
  sorted_collection  = collection;
}
//Формируем окончательный ответ с предложениями (поля text и position)
json Suggestions::suggest_to_response(const std::string& input) {
  json response;
  for (size_t i = 0, m = 0; i < sorted_collection.size(); ++i) {
    if (input == sorted_collection[i].at("id")) {
      //формируем одно предложение
      json one_suggest;
      one_suggest["text"] = sorted_collection[i].at("name");
      one_suggest["position"] = m++;
      //кладём в объект - ответ все предложения
      response["suggestions"].push_back(one_suggest);
    }
  }
  return response;
}
