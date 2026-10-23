# Emulador CCE MC-1000 para TTGO ESP32 VGA32 por André Chaves 🕹️📺

<https://github.com/arfchaves>

Emulador do computador brasileiro **CCE MC-1000** (1985) para a placa
**LilyGO / ROBGO / OLIMEX / TTGO ESP32 VGA32 v1.4**, com imagem VGA, teclado PS/2, saída de
áudio, cartão microSD e interface gráfica de gerenciamento.

O objetivo é preservar a experiência e promover o estudo do hardware e do
software brasileiro dos anos 1980. Projeto comunitário **não comercial**,
independente da CCE e dos titulares dos componentes de hardware e software.

> **Versão 0.7 — port experimental (projeto PlatformIO).** A rotina CAS segue
> o protocolo em dois estágios da ROM (cabeçalho em `$C162`, dados em `$C188`).
> Subpastas de CAS, arquivos `.BAS`, `bootl.rc`, botão mágico e fallback do pino
> MISO do SD são recentes e **ainda precisam de testes na placa**. Não se anunciam
> funcionalidades não verificadas como totalmente compatíveis.

> **Licenciamento:** o código autoral/derivado de fontes MIT incluído aqui está
> sob **MIT**, mas a biblioteca externa **FabGL 1.0.9 é GPLv3**, e a ROM do
> MC-1000 não está licenciada para redistribuição neste repositório.
> Portanto, **não declare o firmware completo como 100% MIT**. Leia
> [Licenciamento e ROM](LICENSING_AND_ROM.md) antes de publicar binários.

## 🚀 Funcionalidades

* **Z80 (3,579545 MHz):** emulação em nível de instrução com contabilização
  de ciclos; núcleo independente (MIT) introduzido na versão 0.0.3.

* **Vídeo MC6847:** texto, semigráficos e modos gráficos MC-1000; VGA por
  FabGL, sinal **640×480 @ 60 Hz** (framebuffer 320×240 com scan doubling),
  pixels do MC6847 duplicados exatamente 2×2 para preservar a proporção dos
  caracteres. Artefatos de cor (OFF/STD/REV), blocos coloridos e bordas
  alternativas configuráveis no menu.

* **Teclado PS/2:** mapeamento de contatos do MC-1000, com layout **EN-US** ou
  **ABNT2** (selecionável no menu) e combinação SHIFT/CTRL, baseado nas
  referências do jsMC1000.

* **Joysticks virtuais:** A (setas + ALT direito) e B (WASD + espaço),
  desligados por padrão e ativados no menu F1.

* **Áudio:** tom, ruído compartilhado e envelopes AY-3-8910 com saída pelo
  conector de áudio do TTGO VGA32; a ROM determina o clique de teclado.

* **OSD:** menu de configuração F1, navegadores de arquivos (F2 fitas `.CAS`,
  F3 jogos `.BIN`, F4 programas `.TXT`/`.BAS`) e indicador de FPS/velocidade do Z80.

* **Cartão microSD:** ROM, fitas `.CAS` em `/MC-1000/CAS` (com navegação em
  subpastas), jogos `.BIN` e programas BASIC em texto. Carregamento em dois
  estágios conforme a ROM; também suporta `SAVE*`/`LOAD*` de matrizes.

* **TLOAD / TSAVE:** carregamento de jogos `.BIN` e editor para gravar `.BIN`
  (nome, início, fim e endereço de execução).

* **Injeção de programas BASIC:** listagens em texto (`.TXT` ou `.BAS`) são
  tokenizadas e colocadas diretamente na memória (F4).

* **Compatibilidade com** **[ESP32 Bootloader (ESP-WORKS)](https://github.com/ESP-WORKS/ESP32_Bootloader):** projeto preparado para integração com o ESP32 Bootloader, com compatibilidade total declarada para o fluxo de inicialização utilizado pelo MC-1000. A integração deve ser validada na placa e na versão do bootloader escolhida.

* **Modo de RAM expandida:** configuração de emulação com 64 KiB de espaço RAM
  (conforme os bancos mapeados no modelo do projeto).

* **Configuração por `bootl.rc`:** pinos do teclado PS/2 e botão mágico
  (veja abaixo).

## 🛠️ Hardware necessário

1. Placa **LilyGO TTGO VGA32 v1.4**, ROBGO, OLIMEX ou outra com ESP32-WROVER.
2. Monitor com entrada **VGA** (preferencialmente 640×480/60 Hz; o ajuste automático do Aitek
   ainda necessita confirmação).
3. Teclado **PS/2 real** ou teclado USB que efetivamente suporte protocolo PS/2.
4. Cartão **microSD FAT32** — **obrigatório**, pois a ROM é lida do cartão.
5. Cabo/fone P2 de 3,5 mm para saída do áudio da placa.
6. Cabo USB para alimentação e gravação do ESP32.

O SD usa CS=13, CLK=14 e MOSI=12. O emulador tenta **MISO=2** (LilyGO) e, se
falhar, **MISO=35** (ROBGO).

## 🔒 ROM do MC-1000: fornecida pelo próprio usuário

**Este repositório NÃO contém a ROM/BASIC original da CCE.** O encerramento da
empresa não comprova ingresso da ROM em domínio público. Você precisa possuir
sua própria cópia legalmente obtida de **16.384 bytes**.

A ROM **não é compilada no firmware**: o emulador a lê do cartão em
`/MC-1000/ROMS/MC-1000.ROM` (maiúsculas/minúsculas tanto faz). Sem ROM válida, a
tela mostra `ROM ERROR: CHECK SD` e o Z80 não inicia.

Consulte [LICENSING\_AND\_ROM.md](LICENSING_AND_ROM.md) para detalhes e fontes
jurídicas.

## 📂 Como preparar o cartão SD

1. Formate o cartão em **FAT32**.
2. Monte esta estrutura:

```text
/bootl.rc                    (opcional) pinos do teclado e botão mágico
/MC-1000/ROMS/MC-1000.ROM    ROM de 16.384 bytes (obrigatória)
/MC-1000/CAS/                fitas .CAS (pode ter subpastas)
/MC-1000/BIN/                jogos .BIN (TLOAD)
/MC-1000/TXT/                programas BASIC em texto (.TXT / .BAS)
```

   `/MC-1000/` e `CAS/` são criadas no boot; `BIN/` e `TXT/` na primeira vez em
   que são usadas. **A pasta `ROMS/` deve ser criada por você.**

3. Com o emulador funcionando, use **F2** para selecionar o arquivo `.CAS`;
   em seguida, no BASIC, execute **`LOAD`** conforme as rotinas da ROM.
   No navegador, **ENTER** entra numa pasta e `[..]` volta; a pasta escolhida
   vale para os próximos LOAD/SAVE.

**SAVE/LOAD:** ao salvar, o emulador cria primeiro um arquivo `.TMP`, confere
o tamanho e tenta renomeá-lo para `.CAS`. Em falhas, examine o cartão, as
mensagens na tela e o monitor serial a **115200 baud**. Durante LOAD, o
cabeçalho é lido antes da comparação do nome; os dados só são transferidos
na segunda chamada da ROM. Um CAS incompatível/truncado deve ser recusado
sem escrever parcialmente no BASIC.

### `bootl.rc`

Arquivo de texto opcional na raiz do SD, compartilhado com o bootloader:

```text
# comentário
kbddat=32     # PS/2 DATA
kbdclk=33     # PS/2 CLOCK
magicb=36     # botão mágico: abre/fecha o menu F1 (ativo em nível baixo)
```

Também aceita `chave valor` ou `chave: valor`. Sem o arquivo, ou com pino
inválido, valem os padrões 32/33 e não há botão mágico. Pinos PS/2 aceitos:
0, 4, 15, 26, 27, 32 e 33.

## 🕹️ Teclado e menus

| Tecla PS/2       | Ação                                                            |
| ---------------- | --------------------------------------------------------------- |
| `F1`             | Abre/fecha o menu principal                                     |
| `F2`             | Abre o navegador de fitas `.CAS` do cartão SD                   |
| `F3`             | Abre o navegador de jogos `.BIN`                                |
| `F4`             | Injeta programa BASIC (`.TXT` / `.BAS`)                         |
| `F12`            | Tecla RESET do MC-1000; com o menu F1 aberto, reseta o emulador |
| `Shift`/`Ctrl` + `F12` | Reset do MC-1000 emulado                                  |
| `ESC`            | Fecha menus; fora deles segue mapeamento específico do emulador |
| `Enter`          | Return/confirmar seleção                                        |
| `Backspace`      | RUBOUT                                                          |
| `Ctrl` e `Shift` | Modificadores do teclado do MC-1000                             |

O indicador de FPS/velocidade do Z80 é ligado em **EXIBIR FPS** no menu F1.

Alguns caracteres especiais dependem da disposição **EN-US** do teclado (ou
ABNT2, se selecionada no menu). O mapeamento é inspirado no
`PC_FRIENDLY_MAPPING` do emulador jsMC1000 criado pelo amigo Emerson Costa.

## 💻 Compilação e gravação

**Ambiente:**

* **VS Code** com a extensão **PlatformIO IDE**;

* Plataforma **espressif32@6.9.0** (Arduino-ESP32 **2.0.17** — a FabGL 1.0.9
  não funciona no core 3.x);

* Biblioteca externa **FabGL 1.0.9** (**GPL-3.0**), baixada automaticamente
  pelo `platformio.ini`;

Abra a pasta do projeto no VS Code e use **Build** (✓) e **Upload** (→) da barra
do PlatformIO (ambiente `ttgo-vga32`). O `partitions.csv` é compatível com o
bootloader ESP-WORKS; para usar com ele, copie
`.pio/build/ttgo-vga32/firmware.bin` para o cartão.

**Não compartilhe o firmware compilado publicamente** antes de verificar os
direitos de ROM e as obrigações GPL aplicáveis à FabGL.

## 🧩 Estrutura do projeto

```text
MC-1000-PlatformIO/
├── platformio.ini                 # Configuração do PlatformIO (placa, FabGL, flags)
├── partitions.csv                 # Tabela de partições compatível com o bootloader
├── src/
│   ├── main.cpp                   # Inicialização e tarefas ESP32
│   ├── mc1000.cpp/.h              # Máquina, memória, teclado, SD/CAS
│   ├── z80/Z80.cpp/.h             # Núcleo Z80, MIT
│   ├── ay38910.cpp/.h             # PSG AY-3-8910, MIT
│   ├── mc6847.cpp/.h              # Vídeo, fonte do jsMC1000 (MIT) e molduras
│   ├── osd.cpp/.h                 # Menus, navegadores e avisos
│   ├── rom_sd.cpp/.h              # Leitura da ROM do cartão SD
│   ├── sysdep.cpp/.h              # Cartão SD, pastas e fallback do MISO
│   ├── bootl_rc.cpp/.h            # Leitura do bootl.rc
│   └── basic_inject.h             # Tokenizador BASIC para .TXT/.BAS
├── LICENSE                        # MIT para os componentes aplicáveis
├── licenses/jsMC1000-MIT.txt      # Aviso MIT original do autor da fonte
├── licenses/MAME-MC6847-BSD-3-Clause.txt
├── licenses/FabGL-GPLv3-NOTICE.md # Aviso GPL da dependência externa
├── THIRD_PARTY_NOTICES.md         # Origem, créditos e licenças
└── LICENSING_AND_ROM.md           # Limitações da MIT/GPL e política de ROM
```

## 🤝 Contribuições

Pull requests e issues são bem-vindos para compatibilidade CAS, testes de
vídeo, temporização do Z80, precisão do AY, documentação e qualidade das
interfaces F1/F2. Não envie ROMs, dumps, CAS de terceiros sem permissão ou
bibliotecas GPL copiados ao diretório do projeto sem revisão da licença.

## 📜 Licenças, créditos e agradecimentos

Agradecimento especial aos autores e projetos cujas implementações,
referências e estudos contribuíram para este port:

* **Jindor — TTGO VGA32 SPC-1000:** base Arduino/FabGL, plataforma TTGO,
  arquivo/cartão SD e arquitetura inicial de menus. Código base MIT,
  copyright 2024 preservado.

* **Kue-Hwan Sihn (ionique):** emulador do SPC-1000 e contribuição histórica
  à evolução da base. O núcleo antigo com restrições foi retirado deste port.

* **Emerson José Silveira da Costa (Ensjo) — jsMC1000:** descrição do teclado
  MC-1000, referência de mapeamento de teclas, documentação de periféricos e
  tabela de caracteres do MC6847. **Fonte MIT reutilizada com atribuição**.

* **Fabrizio Di Vittorio —** **[FabGL](https://github.com/fdivitto/FabGL):** VGA,
  PS/2 e áudio no ESP32. **Dependência GPL-3.0**, não incluída no repositório.

* **Reinaldo Torres / CoCo Byte Club —** **[TTGO VGA32 CoCo 2/3](https://github.com/reyco2000/TTGO-VGA32-COCO):** referências sobre MC6847 e geração VGA.
  O código GPL desse emulador **não** integra esta versão.

* **Víctor Iborra, David Crespo e colaboradores —** **[ESPectrum](https://github.com/EremusOne/ESPectrum):** inspiração visual dos menus e estudos de
  temporização/áudio; código GPL **não incorporado**.

* **[ESP-WORKS — ESP32 Bootloader](https://github.com/ESP-WORKS/ESP32_Bootloader):** agradecimento pelo projeto de bootloader para ESP32 e pela possibilidade de integração com o emulador MC-1000. Este crédito não implica incorporação do código do bootloader nem endosso de seus autores.

* **[MAME](https://github.com/mamedev/mame):** informações de hardware,
  mapeamento dos bancos de memória, modos do VDG e referência da paleta.

* **Comunidade** **[CCE MC-1000](https://sites.google.com/site/ccemc1000/p%C3%A1gina-inicial):** documentação histórica, guias de BASIC e formato de fita.

* **Marat Fayzullin:** reconhecemos historicamente suas contribuições aos
  emuladores Z80/AY; seus módulos antigos **não são distribuídos** nesta versão.

* Aos autores de utilitários, pesquisadores e usuários que preservam a memória
  da computação brasileira.