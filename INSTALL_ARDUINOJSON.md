# Dependência: ArduinoJson 6.x

Este projeto usa **PlatformIO**, e a dependência já está declarada no `platformio.ini`:

```ini
lib_deps =
	bblanchon/ArduinoJson@^6.21.5
```

> **Não é necessário instalar nada à mão.** Rode `pio run` e o PlatformIO baixa a plataforma
> `espressif8266` e o ArduinoJson automaticamente, em um cache local do projeto (`.pio/`).

---

## Se o build falhar em `ArduinoJson.h`

| Sintoma | O que fazer |
|---|---|
| `fatal error: ArduinoJson.h: No such file or directory` | `pio pkg install` (ou apague `.pio/` e rode `pio run` de novo) |
| Erros de API (`StaticJsonDocument` não existe, `JsonDocument` exige alocador) | Uma versão 7.x foi resolvida. Fixe a 6.x no `platformio.ini` e limpe: `pio run --target clean` |
| Download falha / timeout | Rede corporativa bloqueando o registry. Configure o proxy (`HTTP_PROXY` / `HTTPS_PROXY`) ou instale offline (ver abaixo) |

Para conferir o que foi resolvido:

```bash
pio pkg list           # dependências instaladas neste projeto
pio pkg outdated       # versões disponíveis
```

---

## Por que 6.x, e não 7.x

O código usa a API do ArduinoJson **6** — `StaticJsonDocument<N>` e o parse zero-copy via buffer
mutável. A 7.x removeu os documentos estáticos em favor de alocação
dinâmica, o que muda a assinatura do parse e o orçamento de memória do firmware.

| Versão | Status | Observação |
|:---:|:---:|---|
| 6.21.x | ✅ | Versão de referência do projeto |
| 6.22.x | ✅ | Compatível (mesma API) |
| 7.x | ❌ | API incompatível — `StaticJsonDocument` foi removido |
| 5.x | ❌ | API antiga |

---

## Instalação offline (fallback)

Se o registry do PlatformIO estiver inacessível:

1. Baixe o `.zip` de uma release **6.x** em <https://github.com/bblanchon/ArduinoJson/releases>
2. Extraia em `lib/ArduinoJson/` na raiz do projeto — o PlatformIO resolve `lib/` antes do registry
3. Rode `pio run`

---

<details>
<summary><b>Legado: instalação pelo Arduino IDE</b></summary>

<br>

Relevante apenas se você compilar o código fora do PlatformIO.

1. `Sketch` → `Include Library` → `Manage Libraries…` (`Ctrl+Shift+I`)
2. Busque `ArduinoJson` e localize **"ArduinoJson by Benoit Blanchon"**
3. Selecione uma versão **6.x** (ex.: 6.21.5) — **não** instale 5.x nem 7.x — e clique em `Install`
4. Feche e reabra o Library Manager, então recompile (`Ctrl+R`)

Alternativa manual: baixe o `.zip` da release 6.x e use
`Sketch` → `Include Library` → `Add .ZIP Library…`, depois reinicie o IDE.

Se o erro persistir, confirme a board selecionada em
`Tools` → `Board` → `LOLIN(WEMOS) D1 R2 & mini` e limpe o cache de build
(`%LOCALAPPDATA%\Temp\arduino_*` no Windows) com o IDE fechado.

</details>

---

## Referências

- [Documentação do ArduinoJson](https://arduinojson.org/)
- [ArduinoJson no GitHub](https://github.com/bblanchon/ArduinoJson)
- [Migração 6 → 7](https://arduinojson.org/v7/how-to/upgrade-from-v6/)
- [PlatformIO — `lib_deps`](https://docs.platformio.org/en/latest/projectconf/sections/env/options/library/lib_deps.html)
