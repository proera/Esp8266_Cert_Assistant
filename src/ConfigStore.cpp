/*
 * ConfigStore.cpp
 *
 * Implementação da configuração persistente (NVS via Preferences).
 */

#include "ConfigStore.h"

#include <Preferences.h>

ConfigStore config;

static const char* kNamespace = "certmind";

void ConfigStore::begin() {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) {
    // Namespace ainda não existe (1º boot sem nenhum override): defaults valem.
    Serial.println(F("[CFG] NVS vazio — usando defaults do Config.h"));
    return;
  }
  // isKey() antes de cada leitura: chave ausente mantém o default do Config.h
  // (getString com buffer não toca o buffer quando a chave não existe, mas o
  // isKey deixa a intenção explícita).
  if (p.isKey("ssid")) p.getString("ssid", _ssid, sizeof(_ssid));
  if (p.isKey("pass")) p.getString("pass", _pass, sizeof(_pass));
  if (p.isKey("host")) p.getString("host", _host, sizeof(_host));
  if (p.isKey("port")) _port = p.getUShort("port", _port);
  if (p.isKey("path")) p.getString("path", _path, sizeof(_path));
  p.end();

  Serial.println(F("[CFG] Configuração carregada (NVS sobre defaults):"));
  printTo(Serial);
}

bool ConfigStore::set(const char* key, const char* value) {
  if (!key || !value || !value[0]) return false;

  // Valida chave + teto do valor (o teto é o do buffer em RAM correspondente).
  size_t cap = 0;
  if      (strcmp(key, "ssid") == 0) cap = sizeof(_ssid);
  else if (strcmp(key, "pass") == 0) cap = sizeof(_pass);
  else if (strcmp(key, "host") == 0) cap = sizeof(_host);
  else if (strcmp(key, "path") == 0) cap = sizeof(_path);
  else if (strcmp(key, "port") != 0) return false;

  uint16_t portVal = 0;
  if (strcmp(key, "port") == 0) {
    char* end = nullptr;
    unsigned long v = strtoul(value, &end, 10);
    if (end == value || *end != '\0' || v < 1 || v > 65535) return false;
    portVal = (uint16_t)v;
  } else if (strlen(value) >= cap) {
    return false;
  }

  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/false)) return false;
  if (strcmp(key, "port") == 0) {
    p.putUShort("port", portVal);
  } else {
    p.putString(key, value);
  }
  p.end();
  return true;
}

void ConfigStore::clearAll() {
  Preferences p;
  if (p.begin(kNamespace, /*readOnly=*/false)) {
    p.clear();
    p.end();
  }
}

void ConfigStore::printTo(Stream& s) const {
  s.print(F("[CFG]   ssid = "));
  s.println(_ssid);
  // Senha mascarada: comprimento + 2 primeiros chars bastam para conferir
  // qual senha está ativa sem expô-la inteira no log.
  s.print(F("[CFG]   pass = "));
  if (_pass[0]) {
    s.print(_pass[0]);
    if (_pass[1]) s.print(_pass[1]);
    s.print(F("*** ("));
    s.print(strlen(_pass));
    s.println(F(" chars)"));
  } else {
    s.println(F("(vazia)"));
  }
  s.print(F("[CFG]   host = "));
  s.println(_host);
  s.print(F("[CFG]   port = "));
  s.println(_port);
  s.print(F("[CFG]   path = "));
  s.println(_path);
}
