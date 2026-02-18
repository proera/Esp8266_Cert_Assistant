# Sistema de Polling ESP8266 - D1 Mini

Sistema de polling que conecta o D1 Mini (ESP8266) à API de certificação, fazendo requisições HTTP a cada 2 segundos e controlando LEDs baseado nas respostas corretas recebidas.

## 📋 Requisitos

### Hardware
- **1x D1 Mini (ESP8266)** ou clone Wemos
- **5x LEDs** com as seguintes cores:
  - 1x LED Verde (Resposta A)
  - 1x LED Amarelo (Resposta B)
  - 1x LED Vermelho (Resposta C)
  - 1x LED Azul (Resposta D)
  - 1x LED Branco (Resposta E)
- **5x Resistores 220Ω** (um para cada LED)
- **Protoboard e jumpers**
- **Cabo USB Micro-B** para programação

### Software
- **Arduino IDE** versão 1.8.x ou superior
- **Biblioteca ArduinoJson** versão 6.21.0 ou superior
- **ESP8266 Board Package** instalado

## 🔧 Configuração do Arduino IDE

### 1. Instalar ESP8266 Board Package

1. Abra o Arduino IDE
2. Vá em `File` → `Preferences`
3. Em "Additional Board Manager URLs", adicione:
   ```
   http://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
4. Clique em `OK`
5. Vá em `Tools` → `Board` → `Boards Manager`
6. Procure por "esp8266"
7. Instale o pacote **"esp8266 by ESP8266 Community"**

### 2. Instalar Biblioteca ArduinoJson

1. Vá em `Sketch` → `Include Library` → `Manage Libraries`
2. Procure por "ArduinoJson"
3. Instale **"ArduinoJson by Benoit Blanchon"** (versão 6.21.0 ou superior)

### 3. Configurar a Board

1. Vá em `Tools` → `Board` → `ESP8266 Boards`
2. Selecione **"LOLIN(WEMOS) D1 R2 & mini"**
3. Configure as opções:
   - **Upload Speed**: 921600
   - **CPU Frequency**: 80 MHz
   - **Flash Size**: 4MB (FS:2MB OTA:~1019KB)
   - **Port**: Selecione a porta COM correta do seu D1 Mini

## 🔌 Esquema de Conexão

### Pinagem dos LEDs

| LED | Cor | Pino D1 Mini | GPIO | Resistor |
|-----|-----|--------------|------|----------|
| A | Verde | D3 | GPIO0 | 220Ω |
| B | Amarelo | D2 | GPIO4 | 220Ω |
| C | Vermelho | D5 | GPIO14 | 220Ω |
| D | Azul | D1 | GPIO5 | 220Ω |
| E | Branco | D7 | GPIO13 | 220Ω |

### Diagrama de Conexão

```
D1 Mini          Resistor      LED
--------        --------      -----
D3 (GPIO0)  --> 220Ω --> (+) LED Verde (-) --> GND
D2 (GPIO4)  --> 220Ω --> (+) LED Amarelo (-) --> GND
D5 (GPIO14) --> 220Ω --> (+) LED Vermelho (-) --> GND
D1 (GPIO5)  --> 220Ω --> (+) LED Azul (-) --> GND
D7 (GPIO13) --> 220Ω --> (+) LED Branco (-) --> GND
```

**Observação**: Conecte o terminal negativo (-) de cada LED ao GND do D1 Mini.

## ⚙️ Configuração do Código

### 1. Editar Credenciais WiFi

Abra o arquivo `front_cert_assistant.ino` e edite as seguintes linhas (aproximadamente linha 20-21):

```cpp
const char* ssid = "SEU_WIFI_SSID";           // Alterar para o nome da sua rede WiFi
const char* password = "SUA_SENHA_WIFI";      // Alterar para a senha da sua rede WiFi
```

**Exemplo:**
```cpp
const char* ssid = "MinhaRedeWiFi";
const char* password = "minhaSenha123";
```

### 2. Verificar Endpoint da API

O endpoint está configurado na linha 27:

```cpp
const char* apiUrl = "https://certapi.proera.com.br/api/Esp8266/poll";
```

**Não é necessário alterar** este valor, a menos que o endpoint da API mude.

## 📤 Upload do Código

1. Conecte o D1 Mini ao computador via cabo USB
2. Abra o arquivo `front_cert_assistant.ino` no Arduino IDE
3. Verifique se a board e porta estão corretas em `Tools`
4. Clique em `Upload` (ou pressione `Ctrl+U`)
5. Aguarde a compilação e upload do código
6. Após o upload, abra o Serial Monitor (`Tools` → `Serial Monitor` ou `Ctrl+Shift+M`)
7. Configure o baud rate para **115200**

## 📊 Funcionamento

### Ciclo de Operação

1. **Inicialização**
   - O D1 Mini conecta-se ao WiFi
   - Todos os LEDs são inicializados e desligados
   - Sistema entra em modo de polling

2. **Polling (a cada 2 segundos)**
   - Faz requisição GET ao endpoint da API
   - Recebe resposta JSON
   - Extrai campos `isMultipleChoice` e `correctAnswers`
   - Atualiza LEDs baseado nas respostas corretas
   - Exibe dados na Serial

3. **Controle de LEDs**
   - LEDs acendem de acordo com as respostas corretas
   - Se `correctAnswers = ["A", "C"]`, apenas LEDs A e C acendem
   - Demais LEDs permanecem apagados

### Exemplo de Saída Serial

```
╔═══════════════════════════════════════════════╗
║  Sistema de Polling ESP8266 - D1 Mini        ║
║  Versão: 1.0                                  ║
╚═══════════════════════════════════════════════╝
✓ LEDs inicializados

=================================
Conectando ao WiFi: MinhaRedeWiFi
=================================
...
✓ WiFi conectado com sucesso!
Endereço IP: 192.168.1.100
=================================

✓ Setup concluído!

Iniciando polling...

╔════════════════════════════════════════╗
║ Poll #1 - Requisição iniciada...
╚════════════════════════════════════════╝
Status HTTP: 200
✓ Response recebido com sucesso!

--- Dados Extraídos ---
isMultipleChoice: false
correctAnswers: [A]
-----------------------

LEDs atualizados:
  ✓ LED A (Verde): LIGADO
    LED B (Amarelo): DESLIGADO
    LED C (Vermelho): DESLIGADO
    LED D (Azul): DESLIGADO
    LED E (Branco): DESLIGADO

⏳ Aguardando próximo poll (2 segundos)...
```

## 🔍 Estrutura do JSON

### Resposta da API

```json
{
  "hasData": true,
  "itemId": "c61b5e4b-67a0-4548-9500-7e90ecd619b8",
  "data": {
    "isMultipleChoice": false,
    "correctAnswers": ["A"],
    "explanation": "Explicação da resposta...",
    "analyzedAt": "2026-01-09T20:18:28.6459736Z"
  },
  "message": "Data retrieved successfully"
}
```

### Dados Extraídos

O sistema extrai apenas dois campos:

1. **`data.isMultipleChoice`** (boolean)
   - `true`: Questão de múltipla escolha
   - `false`: Questão de escolha única

2. **`data.correctAnswers`** (array de strings)
   - Contém as letras das respostas corretas
   - Exemplo: `["A"]`, `["A", "C", "E"]`

## 🛠️ Solução de Problemas

### WiFi não conecta

**Problema**: LEDs não acendem e Serial mostra "✗ Falha ao conectar ao WiFi!"

**Soluções**:
1. Verifique se o SSID e senha estão corretos
2. Certifique-se de que a rede é 2.4GHz (ESP8266 não suporta 5GHz)
3. Aproxime o D1 Mini do roteador
4. Reinicie o D1 Mini pressionando o botão RESET

### Erro HTTP na requisição

**Problema**: Serial mostra "✗ Erro HTTP: XXX"

**Soluções**:
1. Verifique se o endpoint da API está acessível
2. Teste o endpoint manualmente com curl ou Postman
3. Verifique se há firewall bloqueando a conexão
4. Certifique-se de que a data/hora do sistema está correta

### LEDs não acendem

**Problema**: WiFi conecta e requisição funciona, mas LEDs não acendem

**Soluções**:
1. Verifique as conexões dos LEDs
2. Teste cada LED individualmente com um código simples
3. Verifique se os resistores estão corretos (220Ω)
4. Certifique-se de que a polaridade dos LEDs está correta (+/-)
5. Meça a tensão nos pinos com multímetro

### Biblioteca não encontrada

**Problema**: Erro de compilação "ESP8266WiFi.h: No such file or directory"

**Soluções**:
1. Instale o ESP8266 Board Package conforme instruções acima
2. Reinicie o Arduino IDE
3. Verifique se a board correta está selecionada em `Tools` → `Board`

### ArduinoJson não encontrado

**Problema**: Erro de compilação "ArduinoJson.h: No such file or directory"

**Soluções**:
1. Instale a biblioteca ArduinoJson via Library Manager
2. Certifique-se de instalar a versão 6.x (não a 5.x)
3. Reinicie o Arduino IDE

## 📝 Personalização

### Alterar Intervalo de Polling

Para mudar o intervalo de polling de 2 segundos para outro valor, edite a linha 32:

```cpp
const unsigned long POLLING_INTERVAL = 2000;  // Valor em milissegundos
```

**Exemplos**:
- 5 segundos: `5000`
- 10 segundos: `10000`
- 30 segundos: `30000`

### Alterar Pinos dos LEDs

Para usar outros pinos, edite as linhas 29-33:

```cpp
#define LED_A D3  // Altere D3 para outro pino (ex: D4)
#define LED_B D2  // Altere D2 para outro pino
// ... e assim por diante
```

**Pinos disponíveis no D1 Mini**: D0, D1, D2, D3, D4, D5, D6, D7, D8

## 📚 Recursos Adicionais

- [Documentação ESP8266](https://arduino-esp8266.readthedocs.io/)
- [Pinout D1 Mini](https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/)
- [ArduinoJson Documentation](https://arduinojson.org/)
- [Plano Detalhado do Projeto](plans/plano_detalhado.md)

## 📄 Licença

Este projeto foi desenvolvido para fins educacionais e de certificação.

## 👤 Autor

Proera - 2026

---

**Desenvolvido com ❤️ para o D1 Mini**
