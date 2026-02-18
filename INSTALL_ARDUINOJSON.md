# Como Instalar a Biblioteca ArduinoJson

O erro indica que a biblioteca **ArduinoJson** não está instalada no seu Arduino IDE. Siga os passos abaixo para resolver:

## 🔧 Solução: Instalar ArduinoJson via Library Manager

### Passo a Passo

1. **Abra o Arduino IDE**

2. **Acesse o Library Manager**
   - Clique em `Sketch` → `Include Library` → `Manage Libraries...`
   - Ou use o atalho: `Ctrl+Shift+I`

3. **Procure por ArduinoJson**
   - Na barra de busca, digite: `ArduinoJson`
   - Aguarde carregar os resultados

4. **Instale a biblioteca**
   - Encontre **"ArduinoJson by Benoit Blanchon"**
   - **IMPORTANTE**: Instale a versão **6.x** (exemplo: 6.21.5, 6.22.0)
   - **NÃO instale** a versão 5.x ou 7.x
   - Clique no botão `Install`

5. **Aguarde a instalação**
   - O Arduino IDE baixará e instalará a biblioteca
   - Você verá "INSTALLED" quando concluir

6. **Feche e reabra o Library Manager**

7. **Compile novamente o código**
   - Clique em `Verify` (✓) ou pressione `Ctrl+R`
   - O erro deve desaparecer

## ✅ Verificação

Após instalar, você pode verificar se a biblioteca foi instalada corretamente:

1. Vá em `Sketch` → `Include Library`
2. Role a lista e procure por **ArduinoJson**
3. Se aparecer na lista, a instalação foi bem-sucedida

## 📹 Demonstração Visual

```
Arduino IDE
└── Sketch
    └── Include Library
        └── Manage Libraries...
            └── [Barra de busca: "ArduinoJson"]
                └── ArduinoJson by Benoit Blanchon
                    └── Versão: 6.21.5 (ou superior 6.x)
                        └── [Botão: Install]
```

## 🛠️ Solução Alternativa (Manual)

Se o Library Manager não funcionar, você pode instalar manualmente:

1. **Baixe a biblioteca**
   - Acesse: https://github.com/bblanchon/ArduinoJson/releases
   - Baixe a versão mais recente 6.x (arquivo `.zip`)

2. **Instale via ZIP**
   - No Arduino IDE, vá em `Sketch` → `Include Library` → `Add .ZIP Library...`
   - Selecione o arquivo `.zip` baixado
   - Aguarde a instalação

3. **Reinicie o Arduino IDE**

## 📋 Versões Recomendadas

| Versão | Status | Compatibilidade |
|--------|--------|-----------------|
| 6.21.x | ✅ Recomendada | ESP8266 |
| 6.22.x | ✅ Recomendada | ESP8266 |
| 7.x | ⚠️ Beta | Pode ter incompatibilidades |
| 5.x | ❌ Antiga | API diferente |

## ❓ Ainda com Problemas?

Se após instalar a biblioteca o erro persistir:

1. **Reinicie o Arduino IDE completamente**
2. **Verifique se a board ESP8266 está selecionada**
   - `Tools` → `Board` → `LOLIN(WEMOS) D1 R2 & mini`
3. **Limpe os arquivos temporários**
   - Feche o Arduino IDE
   - Delete a pasta de cache (Windows): `C:\Users\[seu_usuario]\AppData\Local\Temp\arduino_*`
   - Abra o Arduino IDE novamente

## 📚 Referências

- [ArduinoJson Documentation](https://arduinojson.org/)
- [ArduinoJson GitHub](https://github.com/bblanchon/ArduinoJson)
- [Arduino Library Manager Guide](https://docs.arduino.cc/software/ide-v1/tutorials/installing-libraries)

---

Após instalar a biblioteca, o código compilará sem erros!
