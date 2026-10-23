# Créditos, origens e licenças de terceiros

Este projeto é uma adaptação independente para a placa TTGO ESP32 VGA32.
Creditar alguém não implica endosso ou afiliação. Preserve os avisos próprios.

| Autor / projeto | Uso neste projeto | Natureza | Licença / cuidados |
| --- | --- | --- | --- |
| **Jindor** — TTGO VGA32 SPC-1000 | Estrutura original do port para ESP32, integração Arduino/FabGL, gerenciamento de periféricos e base da interface de sistema de arquivos/OSD | Código adaptado | **MIT**, copyright 2024 Jindor, mantido em `LICENSE` |
| **Kue-Hwan Sihn (ionique)** — emulador SPC-1000 | Referência histórica do emulador que antecedeu o port TTGO | Crédito histórico; módulos Z80 antigos retirados | Não reproduzir código com restrições da versão antiga |
| **Emerson José Silveira da Costa (Ensjo)** — `jsMC1000` | Matriz de teclado e regras de adaptação do teclado PC; fonte de caracteres `MC6847.FONT` incorporada em `src/mc6847.cpp`; lógica do mod de blocos coloridos em `src/mc6847_ensjo_mod.h`; formato/tokenização BASIC de `MC1000.js` adaptado em `src/basic_inject.h`; informações de hardware | Adaptação de dados/implementação MIT e referência | **MIT**, copyright 2013 Emerson José Silveira da Costa. Preservada a identificação no arquivo de fonte e texto integral em `licenses/jsMC1000-MIT.txt`. Origem: pacote `jsmc1000-master.zip`/projeto `jsMC1000` |
| **Fabrizio Di Vittorio** — [FabGL](https://github.com/fdivitto/FabGL) | **Dependência externa obrigatória** para VGA, PS/2 e áudio | Não vendorizada | **GPL-3.0**, copyright 2019–2022. A MIT deste repositório não licencia a FabGL |
| **Reinaldo Torres / CoCo Byte Club** — [TTGO VGA32 CoCo 2/3](https://github.com/reyco2000/TTGO-VGA32-COCO) | Referência de temporização VGA e comportamento do MC6847/VDG | Referência técnica; fonte GPL do CoCo não incorporada na versão 0.0.3 | **GPL-3.0** no projeto de referência; conferir qualquer futura cópia de código |
| **Equipe ESPectrum / Víctor Iborra e David Crespo** — [ESPectrum](https://github.com/EremusOne/ESPectrum) | Inspiração de organização visual/OSD e comparação de vídeo/áudio, sem cópia de arquivos | Referência visual | **GPL-3.0**, não incluída como código |
| **MAME** — [driver MC-1000](https://github.com/mamedev/mame/blob/master/src/mame/cce/mc1000.cpp) e [MC6847](https://github.com/mamedev/mame/blob/master/src/devices/video/mc6847.cpp) | Mapeamento de memória, registradores, modos de vídeo e referências de valores de paleta | Referência de documentação/hardware. Desde v0.24, a tabela de artefatos RG6 é derivada e pré-quantizada da implementação `mc6847.cpp` de Nathan Woods. Na v0.25, somente o mapeamento elétrico JOY0/JOY1 na matriz AY-3-8910 foi utilizado como referência técnica; a lógica dos joysticks foi escrita independentemente, sem copiar rotinas do driver MAME | A tabela derivada em `src/mc6847_artifacts.h` usa **BSD-3-Clause**, crédito a Nathan Woods e licença em `licenses/MAME-MC6847-BSD-3-Clause.txt`; demais partes mantêm licenças próprias |
| **Comunidade CCE MC-1000** — [repositório técnico](https://sites.google.com/site/ccemc1000/p%C3%A1gina-inicial) | Documentação do BASIC, dos periféricos, do formato de fita e contexto histórico | Referência documental | Créditos das respectivas páginas e seus autores |
| **Motorola / General Instrument / Zilog** | MC6847, AY-3-8910 e Z80 reproduzidos como componentes de hardware | Descrição funcional de componentes, não software desses fabricantes | Marcas e documentação permanecem de seus titulares |

## ROM externa e arquivos de usuário

A ROM/BASIC original CCE MC-1000 **não faz parte** dos arquivos-fonte, do
executável gerado nem dos arquivos de teste. O emulador apenas lê a ROM de
16 KiB fornecida pelo usuário do microSD em `/MC-1000/ROMS/MC-1000.ROM`.
A marca CCE, a ROM, jogos e fitas `.CAS` de terceiros não são licenciados
pela licença MIT deste projeto. Não publique essas obras sem os direitos
necessários. Os testes de protocolo de ROM recebem opcionalmente um arquivo
local; não embutem dados da ROM. Mudar o suporte físico de armazenamento não
altera a titularidade de direitos autorais.

## Código retirado na transição para 0.0.3

Foram **removidos**, e NÃO são distribuídos neste pacote, os módulos derivados do
emulador Z80/AY e EMULib de **Marat Fayzullin**: `src/z80/Z80SPC.c`, os cabeçalhos
`src/z80/Codes*.h`, `src/z80/Tables.h`, as versões anteriores de `Z80.h`,
`src/AY8910.c`, `src/AY8910.h`, `src/Sound.c`, `src/Sound.h` e `src/EMULib.h`.
Também foi removido `src/mc6847_font.c`, cuja atribuição anterior a "Zanny"
não estabelecia uma licença clara.

A arquitetura anterior se beneficiou historicamente dessas implementações.
Obrigado a Marat Fayzullin pelo trabalho em emulação de sistemas antigos;
**o crédito histórico não deve ser confundido com uma alegação de uso de seu
código na versão nova**. A interface atual foi substituída por uma implementação
independente, com novas fontes sob MIT.

## Preservação de copyright

O `LICENSE` não anula os avisos de autores anteriores. O arquivo
`src/mc6847.cpp` documenta a fonte MIT e o copyright de Ensjo.
As demais partes herdadas do port SPC-1000 preservam crédito a Jindor.
Licenças externas e direitos da ROM são regidos separadamente.
