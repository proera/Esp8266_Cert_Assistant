/*
 * ConfigStore.h
 *
 * Configuração persistente em NVS (M6).
 *
 * Host/porta/path do stream e credenciais WiFi deixam de ser só #define e
 * passam a poder ser sobrescritos em runtime (CLI da serial) com persistência
 * em NVS — uma troca de DHCP do backend se resolve com `config set host ...`
 * + `restart`, sem recompilar. Os #define do Config.h continuam sendo o
 * DEFAULT: valem no primeiro boot e voltam a valer após `config clear`.
 *
 * Concorrência (invariante do dual-core): os getters entregam os valores
 * carregados no boot (RAM) e são lidos pela netTask; set()/clearAll() escrevem
 * SÓ no NVS — nunca na cópia em RAM — e o efeito vale após restart. É isso que
 * dispensa lock entre a CLI (loopTask) e a netTask.
 */

#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <Arduino.h>
#include "Config.h"

class ConfigStore {
  public:
    // Carrega o NVS por cima dos defaults do Config.h. Chamar no setup(),
    // ANTES de criar a netTask (que consome os getters).
    void begin();

    const char* ssid() const { return _ssid; }
    const char* pass() const { return _pass; }
    const char* host() const { return _host; }
    uint16_t    port() const { return _port; }
    const char* path() const { return _path; }

    // Persiste um override no NVS (chaves: ssid, pass, host, port, path).
    // NÃO altera os valores em uso — efeito após restart. false = chave/valor inválido.
    bool set(const char* key, const char* value);

    // Remove todos os overrides (volta aos defaults do Config.h após restart).
    void clearAll();

    // Valores em uso, com a senha mascarada.
    void printTo(Stream& s) const;

  private:
    char _ssid[33]  = WIFI_SSID;
    char _pass[65]  = WIFI_PASSWORD;
    char _host[64]  = STREAM_HOST;
    uint16_t _port  = STREAM_PORT;
    char _path[96]  = STREAM_PATH;
};

extern ConfigStore config;

#endif // CONFIG_STORE_H
