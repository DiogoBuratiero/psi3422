# Carrinho Raspberry Pi com Seguimento Visual e Dígitos Manuscritos

<div style="border-left: 4px solid #f39c12; background:#fff7e6; padding:12px 16px; margin:12px 0; font-family:Arial, sans-serif; font-size:14px;">
  <strong>⚠️ Aviso:</strong> Este projeto foi desenvolvido com apoio de ferramentas de Inteligência Artificial, e todo o código foi posteriormente revisado e validado pelos autores.
</div>

Sistema de controle para um carrinho baseado em Raspberry Pi, comandado por um cliente em PC via TCP.
O carrinho recebe:

1. **Comandos manuais** por um "teclado de mouse" (numpad visual na tela).
2. **Controle automático contínuo** para seguir um **quadrado de referência** visto pela câmera.
3. **Scripts de movimento** disparados por **dígitos manuscritos (0–9)** desenhados dentro desse quadrado.

## Arquitetura do Projeto

### Componentes principais

-   **`server7.cpp`** – Servidor que roda no Raspberry Pi:

    -   Faz interface com GPIO via `wiringPi`/`softPwm`.
    -   Recebe comandos do cliente (teclado, controle analógico, scripts de dígitos).
    -   Captura vídeo da câmera, compacta e envia para o cliente.

-   **`client7.cpp`** (comentários internos como `cliente4.cpp`) – Cliente que roda no PC:

    -   Recebe o vídeo do Raspberry.
    -   Desenha o teclado visual e a imagem da câmera lado a lado.
    -   Localiza, em múltiplas escalas, um template (`quadrado.png`) na cena.
    -   Controla o carrinho para manter o quadrado centralizado.
    -   Detecta dígitos manuscritos no interior do quadrado usando MNIST + FLANN/k-NN.
    -   Envia, a cada frame, o comando apropriado ao servidor:

        -   **1. Controle manual (teclado de mouse)**
            Quando o usuário clica em uma célula do “numpad” visual (teclas 1–9), o cliente entra em modo de override manual e envia ao servidor **apenas 1 byte**:

            -   `'<d>'` – caractere ASCII do dígito pressionado (`'1'` a `'9'`, eventualmente `'0'`/`'5'` como neutro).

        -   **2. Scripts disparados por dígitos manuscritos**
            Quando um dígito manuscrito (0–9) dentro do quadrado é reconhecido de forma estável, o cliente gera **um único comando de script** e, naquele frame, envia **2 bytes**:

            -   `'D'` – cabeçalho indicando “comando de dígito/script”;
            -   `'<d>'` – caractere ASCII do dígito reconhecido (`'0'` a `'9'`).
                Nos frames seguintes, o cliente volta ao modo automático, enquanto o servidor executa o movimento temporizado correspondente.

        -   **3. Seguimento automático do quadrado (controle contínuo)**
            Na ausência de override manual ou de comando de dígito a disparar, o cliente opera em modo automático, calculando as potências das rodas esquerda/direita a partir da posição e do tamanho do quadrado na imagem.
            Em cada frame, ele envia **3 bytes**:
            -   `'A'` – cabeçalho de “controle analógico contínuo”;
            -   `L` – valor `int8` em \[-100, 100\] para a roda esquerda;
            -   `R` – valor `int8` em \[-100, 100\] para a roda direita.

-   **`projeto.hpp` / `raspberry.hpp`**
    Infraestrutura compartilhada:

    -   Tipos básicos (`BYTE`), tratamento de erro (`erro()`).
    -   Classes de rede (`DEVICE`, `SERVER`, `CLIENT`) sobre sockets IPv4/IPv6.
    -   Rotinas auxiliares de processamento de imagem (conversão de tipos, `matchTemplateSame`, etc.).
    -   API para carregar MNIST.

-   **Recursos externos**
    -   `include/quadrado.png` – Template do quadrado a ser seguido.
    -   Diretório `include/mnist` – Base MNIST pré-processada para classificação de dígitos.

## Funcionalidades

### 1. Controle manual – "teclado de mouse"

-   Janela principal:
    -   **Esquerda**: teclado visual 3×3 (layout numpad: 7–8–9 / 4–5–6 / 1–2–3).
    -   **Direita**: vídeo da câmera (com anotações de detecção, HUD, dígitos, etc.)
-   O usuário clica e arrasta com o **botão esquerdo** sobre o teclado:
    -   Enquanto o botão está pressionado, a célula sob o cursor determina o dígito ativo.
    -   O cliente envia esse dígito como **override manual** para o servidor.
-   A tecla **ESC** no teclado físico encerra o cliente, que envia um comando de parada (`'s'`) ao servidor.

### 2. Seguimento automático do quadrado

-   O cliente:
    -   Converte o frame recebido para `float`.
    -   Executa **template matching multi-escala** (CCORR + CCOEFF_NORMED) sobre o modelo do quadrado.
    -   Seleciona os melhores candidatos com supressão de vizinhança e escolhe o de maior NCC como alvo principal.
-   A partir do alvo:
    -   Calcula **erro lateral** `ex` (deslocamento horizontal normalizado em relação ao centro da imagem).
    -   Calcula **tamanho relativo** `frac` (largura do template / largura da imagem).
    -   Ajusta dinamicamente:
        -   **Velocidade de avanço**: aumenta quando o alvo está longe (pequeno) e reduz quando está perto (grande).
        -   **Ganho de esterçamento**: mais agressivo para alvo pequeno, mais suave para alvo grande.
    -   Aplica um **filtro exponencial (EMA)** para suavizar as saídas de potência (L, R) das rodas.
    -   Impõe **faixa segura** `0..100` e zona morta em torno de `ex=0` para evitar oscilações.
-   O cliente envia, a cada frame sem override manual/dígito:
    -   Pacote analógico: `['A', L, R]`
        com `L` e `R` em **int8_t** no intervalo `[-100, 100]`.

### 3. Reconhecimento de dígitos manuscritos

-   Quando o quadrado está grande o suficiente na imagem (`frac` ≥ limite configurado):
    1. O cliente recorta uma **região interna** ao quadrado.
    2. Normaliza o contraste e aplica **limiarização binária (Otsu)**.
    3. Centraliza o conteúdo com `bbox` (normalização de bounding box).
    4. Converte para `float` em `[0,1]` e achata em um vetor de atributos.
-   Classificador:
    -   `DigitFlann`: índice FLANN (KD-tree) sobre os vetores MNIST.
    -   Realiza k-NN com `k=5` e votação de rótulos (0–9).
-   Lógica de disparo:
    -   Um dígito só é considerado **estável** após N frames consecutivos com a mesma predição.
    -   Um comando só pode ser disparado se o sistema estiver **"armado"** (sem dígito por M frames anteriores).
    -   Quando dispara, envia exatamente **uma vez** o dígito como comando de script (`'D'` + dígito) e desarma até não ver dígitos por um tempo.

## Protocolo de Comunicação

### Conexão e handshake

1. O servidor (`SERVER`) abre um socket TCP, aguarda conexão e inicializa PWM e câmera.
2. O cliente (`CLIENT`) conecta ao IP do Raspberry e cria a janela `cliente4`.
3. Handshake inicial:
    - O cliente envia 1 byte (`'0'` na versão atual).
    - O servidor:
        - Se receber `'s'`, encerra imediatamente.
        - Se receber `'A'`, interpreta os próximos 2 bytes como controle analógico inicial (L, R).
        - Caso contrário, tenta tratar o byte como comando de script de dígito; se não for script válido, assume modo teclado (`lastMode='K'`).

Depois do handshake, o ciclo é:

1. **Servidor → Cliente**
    - Captura um frame da câmera (`320×240`), envia imagem compactada.
2. **Cliente → Servidor**
    - Lê teclado visual, estado do seguidor de quadrado e do reconhecedor de dígitos.
    - Envia exatamente um dos formatos:
        - **Parar**: `'s'` (um byte).
        - **Script de dígito**: `['D', '0'..'9']`.
        - **Teclado manual**: `'1'..'9'` (um byte).
        - **Controle analógico contínuo**: `['A', L, R]`.
3. O servidor:
    - Interpreta o cabeçalho (primeiro byte) e atualiza seu estado interno (`lastMode`, `lastKey`, `lastL`, `lastR`).

## Mapeamentos de Comando

### 1. Teclado numérico (controle manual imediato)

No servidor, `applyCommand(char cmd)` converte dígitos em ações de tanque nas rodas:

| Dígito  | Ação (intuitiva)                            |
| ------- | ------------------------------------------- |
| `7`     | Curva suave frente-esquerda.                |
| `8`     | Avançar reto (ambas rodas para frente).     |
| `9`     | Curva suave frente-direita.                 |
| `4`     | Girar sobre o próprio eixo para a esquerda. |
| `5`/`0` | "Neutro" (não altera movimento atual).      |
| `6`     | Girar sobre o próprio eixo para a direita.  |
| `1`     | Curva suave ré-esquerda.                    |
| `2`     | Ré reta (ambas rodas para trás).            |
| `3`     | Curva suave ré-direita.                     |

### 2. Scripts disparados por dígitos manuscritos

`handleDigitScript(char cmd)` mapeia dígitos manuscritos para **movimentos temporizados** (`TimedMotion`):

Constantes principais:

-   `PASS_UNDER_MS = 1500` – tempo para "passar embaixo" (~1,5 s).
-   `TURN90_MS = 550` – giro aproximado de 90°.
-   `TURN180_MS = 1000` – giro aproximado de 180°.

Mapeamento:

| Dígito(s) | Script                                       |
| --------- | -------------------------------------------- |
| `0`, `1`  | Cancela qualquer script em andamento (para). |
| `2`       | Gira ~180° à esquerda.                       |
| `3`       | Gira ~180° à direita.                        |
| `4`, `5`  | Avança reto por `PASS_UNDER_MS`.             |
| `6`, `7`  | Gira ~90° à esquerda.                        |
| `8`, `9`  | Gira ~90° à direita.                         |

Enquanto um `TimedMotion` está ativo, ele tem prioridade sobre o controle analógico/teclado até expirar.

## Dependências

### Servidor (Raspberry Pi)

-   **C++17** (g++).
-   **OpenCV 4** (mínimo: core + imgproc + highgui + videoio).
-   **wiringPi** e `softPwm` para controle de GPIO.
-   Sistema operacional tipo Linux (Raspberry Pi OS ou similar).
-   Câmera compatível com `/dev/video0` (ou índice 0 no OpenCV).

### Cliente (PC)

-   **C++17** (g++).
-   **OpenCV 4** (core, imgproc, highgui, videoio, flann).
-   **OpenMP** (opcional, mas habilitado por padrão).
-   Acesso à rede TCP ao IP do Raspberry.
-   Diretórios:
    -   `include/quadrado.png`
    -   `include/mnist` (dados MNIST no formato esperado pela classe `MNIST`).

## Compilação

### Servidor (no Raspberry Pi)

Exemplo (ajuste conforme seu ambiente OpenCV/wiringPi):

```bash
compila server7 -ocv -v3
```

Certifique-se de que `projeto.hpp` e `raspberry.hpp` estejam no `include path`.

### Cliente (no PC)

O próprio código indica uma linha de compilação típica:

```bash
compila client7 -ocv -v3 -omp
```

## Execução

1. **No Raspberry Pi (servidor)**

    ```bash
    ./server7
    ```

    - Inicializa `wiringPi` e PWM.
    - Abre a câmera.
    - Bloqueia aguardando conexão TCP do cliente.

2. **No PC (cliente)**

    ```bash
    ./cliente4 <ip_raspberry> [saida.avi] [t/c]
    ```

    - `ip_raspberry`: endereço IP do Raspberry Pi.
    - `saida.avi` (opcional): gravação do vídeo composto (teclado+camera).
    - `t` ou `c` (opcional):

        - `t` (default): exibe a tela com anotações (HUD, candidatos, dígito, etc.).
        - `c`: mostra somente a câmera crua (sem overlay) do lado da interface gráfica.

3. **Uso básico**

    - Mova o quadrado físico no campo de visão da câmera (compatível com `quadrado.png`).
    - Use o teclado visual para testar o controle manual.
    - Desenhe dígitos legíveis dentro do quadrado para disparar scripts de movimento.

## Ajustes e Calibração

-   **Pinos de GPIO**:
    Altere `R_REV`, `R_FWD`, `L_FWD`, `L_REV` em `server7.cpp` para refletir o mapeamento real das rodas no seu hardware.

-   **Ganho/velocidades**:

    -   `PWM_HIGH`, tempos de `TURN90_MS`, `TURN180_MS`, `PASS_UNDER_MS` podem ser ajustados para o seu carrinho (peso, motores, bateria).

-   **Seguimento do quadrado**:

    -   Faixas `FRAC_MIN`, `FRAC_MAX`, `FRAC_DIGIT_NEAR`, `THRESH_NCC` ajustam sensibilidade e distância de operação.
    -   Pode ser necessário recalibrar se `quadrado.png` ou a resolução da câmera forem alterados.

-   **Reconhecimento de dígitos**:

    -   Certifique-se de que o diretório MNIST está no formato esperado.
    -   Se o reconhecimento estiver instável, é possível:

        -   Ajustar `DIGIT_STABLE_FRAMES` e `DIGIT_REARM_FRAMES`.
        -   Alterar o número de vizinhos `k` em `DigitFlann::predict`.

## Resumo

Este projeto integra:

-   Controle de motores via PWM no Raspberry Pi.
-   Streaming de vídeo em tempo real pela rede.
-   Interface gráfica interativa (teclado de mouse).
-   Visão computacional para seguimento de um alvo (quadrado).
-   Reconhecimento de dígitos manuscritos com MNIST + FLANN/k-NN.

O resultado é um carrinho capaz de operar tanto **manualmente** quanto em modo **semi-autônomo**, reagindo a **comandos visuais** desenhados diretamente no campo de visão da câmera.
