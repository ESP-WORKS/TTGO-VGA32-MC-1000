# Licenciamento, direitos de terceiros e ROM do CCE MC-1000

> Documento de precaução para publicação do código-fonte. Não constitui parecer jurídico.

## O que é MIT e o que NÃO é

- O código original desenvolvido especificamente para este port e as alterações autorizadas
  a código MIT podem ser publicados sob MIT, preservados os avisos anteriores.
- A tabela de caracteres incorporada em `src/mc6847.cpp` foi regenerada a partir da tabela
  `MC6847.FONT` do **jsMC1000**, de Emerson José Silveira da Costa, distribuído sob MIT.
  Sua autoria original está preservada no próprio arquivo.
- A biblioteca **FabGL 1.0.9**, de Fabrizio Di Vittorio, é distribuída sob **GPL-3.0**
  (não MIT). Ela é uma **dependência externa**, não incluída neste repositório.
  A licença MIT deste repositório **não converte FabGL em MIT**. A distribuição de um
  firmware combinado pode impor requisitos GPLv3; consulte o texto da GPLv3 e obtenha
  orientação jurídica antes de distribuir BINs ou afirmar que o firmware é apenas MIT.
- A ROM/BASIC do MC-1000 NÃO está sob licença MIT e **não é distribuída** aqui.
- Arquivos `.CAS`, jogos, firmware pré-compilado e marcas/nome comercial CCE também
  não são licenciados por este repositório.

## A ROM: o fechamento da CCE NÃO basta

O encerramento das atividades de uma empresa não coloca automaticamente seus programas
no domínio público. A Lei brasileira **9.609/1998, art. 2º, §2º**, prevê proteção do
software por **50 anos contados do 1º de janeiro subsequente à publicação** ou,
na ausência de publicação, à criação. O registro não é condição de proteção.
Os direitos podem subsistir após encerramento societário e/ou pertencer a
sucessores, adquirentes ou outros titulares. Não foi comprovada aqui autorização
para distribuir a ROM ou uma decisão válida de domínio público.

**Decisão técnica atual (após 0.0.8, ainda sem lançamento):** a ROM não é
incorporada ao código-fonte nem ao executável compilado. Em vez disso, o
emulador abre `/MC-1000/ROMS/MC-1000.ROM` do cartão microSD na inicialização,
aceitando qualquer combinação de maiúsculas/minúsculas. O nome base é fixo e
o conteúdo deve ter **exatamente 16.384 bytes**; caso contrário, o Z80 não
inicia. A ROM é copiada para RAM e disponibilizada somente ao endereço de
memória emulado. O programa não oferece ferramenta para obtê-la de terceiros.

**O usuário deve possuir uma cópia legitimamente adquirida/extraída.** A ROM
continua sujeita aos direitos do respectivo titular mesmo que resida no SD.
Uma imagem distribuída como download separado, arquivo de demonstração no
GitHub, asset em Releases, submódulo, Git LFS ou incluída em um arquivo ZIP
continuaria sendo uma distribuição de ROM. **Não a publique sem autorização
válida ou outra base jurídica aplicável.**

Foram removidos do projeto **`src/mc1000_rom.h` e `tools/prepare_rom.py`**.
O arquivo gerado `src/mc1000_rom.c` não pode permanecer na pasta local `src/`,
pois o sistema de compilação Arduino pode compilá-lo automaticamente, mesmo
que esteja ignorado pelo Git. Exclua-o manualmente antes de compilar e faça a
auditoria de publicação.

**Distinção de direitos:** remover a ROM do firmware reduz o risco de
distribuir a própria ROM ao publicar um executável. Isso **não concede**
autorização para compartilhar ROMs nem muda as condições da **FabGL GPLv3**,
do código-base MIT, de programas CAS, de logos, do firmware de terceiros ou
das imagens de jogos. A simples redistribuição de um BIN compilado com FabGL
pode ter obrigações GPLv3 de aviso, licença e código-fonte correspondente,
independentemente de haver ROM no cartão.

## Projeto sem fins comerciais

O caráter gratuito/educacional **não substitui** autorização ou licença e não
elimina obrigações de atribuição. A licença MIT também **permite** usos comerciais
por terceiros: se o objetivo é preservar a restrição a usos comerciais, MIT não
é a licença adequada. Este repositório não reivindica propriedade intelectual
sobre o hardware MC-1000, a ROM/BASIC, a FabGL nem os programas `.CAS` de terceiros.

## Preparação de publicação

1. Publicar apenas os arquivos de código e documentação, depois de confirmar a
   proveniência; NÃO adicionar ROMs, CAS de terceiros ou firmwares não auditados.
2. Preservar `LICENSE`, `THIRD_PARTY_NOTICES.md` e cabeçalhos originais.
3. Mencionar FabGL GPLv3 no README e disponibilizar aos destinatários as
   informações de licença correspondentes, quando aplicável.
4. Solicitar revisão jurídica independente antes de qualquer distribuição pública
   de firmware ou conclusão sobre licenças de conteúdo histórico.
5. Garantir que `src/mc1000_rom.c` não reapareça em computadores com builds
   antigos, e que nenhum commit do histórico Git contenha ROMs.

### Fontes jurídicas e de licença

- Lei de Software brasileira: https://www.planalto.gov.br/ccivil_03/leis/l9609.htm
- Lei de Direitos Autorais: https://www.planalto.gov.br/ccivil_03/leis/l9610.htm
- FabGL: https://github.com/fdivitto/FabGL/blob/master/LICENSE
- MIT: https://opensource.org/licenses/MIT
