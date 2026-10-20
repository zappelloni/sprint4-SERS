/*
  Estacao LightWeek - Sprint 4
  Recarga inteligente de veiculos eletricos com tarifacao dinamica por geracao solar,
  peak shaving e sessoes automatizadas.
  Desafio FIAP + GoodWe | Turma 1CCPK | Disciplina SERS
*/

const int PINO_SOLAR = A0;
const int PINO_LED_VAGA[3] = {13, 12, 11};
const int PINO_LED_SOLAR = 10;

const float POTENCIA_TOTAL_KW = 100.0;
const float POTENCIA_MAX_POR_VEICULO_KW = 50.0;
const float TARIFA_NORMAL = 2.50;
const float TARIFA_VERDE = 1.75;
const int LIMIAR_VERDE_PERCENTUAL = 50;
const unsigned long TEMPO_SESSAO_MS = 30000; // 30s simulados = 1 "ciclo" de recarga

struct Vaga {
  bool ocupada;
  unsigned long inicio;
  float potenciaAtual;
  bool tarifaVerde;
};

struct Sessao {
  int vaga;
  unsigned long duracaoS;
  float energiaKwh;
  float valor;
  float economia;
  bool tarifaVerde;
};

Vaga vagas[3];
Sessao historico[10];
int historicoCount = 0;
int historicoProximo = 0;

int sessoesTotal = 0;
int sessoesVerde = 0;
float energiaTotalKwh = 0;
float receitaTotal = 0;
float economiaTotal = 0;

bool modoVerdeAtivo = false;

void setup() {
  Serial.begin(9600);
  for (int i = 0; i < 3; i++) {
    pinMode(PINO_LED_VAGA[i], OUTPUT);
    vagas[i] = {false, 0, 0, false};
  }
  pinMode(PINO_LED_SOLAR, OUTPUT);
  mostrarAjuda();
}

void loop() {
  atualizarModoVerde();
  verificarEncerramentoAutomatico();

  if (Serial.available() > 0) {
    char comando = Serial.read();
    processarComando(comando);
  }
}

// --- Sensoriamento -----------------------------------------------------

int lerSolarPercentual() {
  int leitura = analogRead(PINO_SOLAR);      // 0-1023
  return map(leitura, 0, 1023, 0, 100);
}

void atualizarModoVerde() {
  bool novoModo = lerSolarPercentual() >= LIMIAR_VERDE_PERCENTUAL;
  if (novoModo != modoVerdeAtivo) {
    modoVerdeAtivo = novoModo;
    digitalWrite(PINO_LED_SOLAR, modoVerdeAtivo ? HIGH : LOW);
    Serial.println();
    Serial.println(modoVerdeAtivo
      ? ">> [AUTO] Geracao solar alta: tarifa VERDE ativada."
      : ">> [AUTO] Geracao solar baixa: tarifa NORMAL ativada.");
  }
}

// --- Peak shaving --------------------------------------------------------

// Recalcula a potencia de cada vaga ocupada sempre que a ocupacao muda,
// dividindo a capacidade total sem ultrapassar o limite por veiculo.
void atualizarPotencias() {
  int ocupadas = contarOcupadas();
  if (ocupadas == 0) return;
  float porVeiculo = min(POTENCIA_MAX_POR_VEICULO_KW, POTENCIA_TOTAL_KW / ocupadas);
  for (int i = 0; i < 3; i++) {
    if (vagas[i].ocupada) {
      vagas[i].potenciaAtual = porVeiculo;
    }
  }
}

int contarOcupadas() {
  int n = 0;
  for (int i = 0; i < 3; i++) if (vagas[i].ocupada) n++;
  return n;
}

// --- Sessoes ---------------------------------------------------------

void abrirVaga(int i) {
  if (vagas[i].ocupada) return;
  vagas[i].ocupada = true;
  vagas[i].inicio = millis();
  vagas[i].tarifaVerde = modoVerdeAtivo;
  digitalWrite(PINO_LED_VAGA[i], HIGH);
  atualizarPotencias();

  Serial.println();
  Serial.print("<< Sessao iniciada na vaga "); Serial.println(i + 1);
  Serial.print("   Potencia alocada: "); Serial.print(vagas[i].potenciaAtual, 2); Serial.println(" kW");
  Serial.print("   Tarifa: "); Serial.println(vagas[i].tarifaVerde ? "VERDE" : "NORMAL");
}

void fecharVaga(int i) {
  if (!vagas[i].ocupada) return;

  unsigned long duracaoMs = millis() - vagas[i].inicio;
  unsigned long duracaoS = duracaoMs / 1000;
  float horas = duracaoMs / 3600000.0;
  float energiaKwh = vagas[i].potenciaAtual * horas;
  float tarifaAplicada = vagas[i].tarifaVerde ? TARIFA_VERDE : TARIFA_NORMAL;
  float valor = energiaKwh * tarifaAplicada;
  float economia = vagas[i].tarifaVerde ? energiaKwh * (TARIFA_NORMAL - TARIFA_VERDE) : 0;

  registrarSessao(i + 1, duracaoS, energiaKwh, valor, economia, vagas[i].tarifaVerde);

  Serial.println();
  Serial.print("<< Sessao encerrada na vaga "); Serial.println(i + 1);
  Serial.print("   Duracao: "); Serial.print(duracaoS); Serial.println(" s");
  Serial.print("   Energia estimada: "); Serial.print(energiaKwh, 4); Serial.println(" kWh");
  Serial.print("   Valor cobrado: R$ "); Serial.println(valor, 2);
  if (vagas[i].tarifaVerde) {
    Serial.print("   Economia (tarifa verde): R$ "); Serial.println(economia, 2);
  }

  vagas[i] = {false, 0, 0, false};
  digitalWrite(PINO_LED_VAGA[i], LOW);
  atualizarPotencias();
}

void registrarSessao(int vaga, unsigned long duracaoS, float energiaKwh, float valor, float economia, bool verde) {
  historico[historicoProximo] = {vaga, duracaoS, energiaKwh, valor, economia, verde};
  historicoProximo = (historicoProximo + 1) % 10;
  if (historicoCount < 10) historicoCount++;

  sessoesTotal++;
  if (verde) sessoesVerde++;
  energiaTotalKwh += energiaKwh;
  receitaTotal += valor;
  economiaTotal += economia;
}

void verificarEncerramentoAutomatico() {
  for (int i = 0; i < 3; i++) {
    if (vagas[i].ocupada && (millis() - vagas[i].inicio) >= TEMPO_SESSAO_MS) {
      Serial.println();
      Serial.println(">> [AUTO] Tempo de sessao atingido, encerrando automaticamente...");
      fecharVaga(i);
    }
  }
}

// --- Comandos ---------------------------------------------------------

void processarComando(char c) {
  switch (c) {
    case '1': case '2': case '3': {
      int i = c - '1';
      if (vagas[i].ocupada) fecharVaga(i); else abrirVaga(i);
      break;
    }
    case '+': {
      for (int i = 0; i < 3; i++) {
        if (!vagas[i].ocupada) { abrirVaga(i); return; }
      }
      Serial.println(">> Nenhuma vaga livre.");
      break;
    }
    case '-': {
      int maisAntiga = -1;
      for (int i = 0; i < 3; i++) {
        if (vagas[i].ocupada && (maisAntiga == -1 || vagas[i].inicio < vagas[maisAntiga].inicio)) {
          maisAntiga = i;
        }
      }
      if (maisAntiga >= 0) fecharVaga(maisAntiga);
      else Serial.println(">> Nenhuma sessao ativa.");
      break;
    }
    case 's': mostrarStatus(); break;
    case 'l': listarSessoesAtivas(); break;
    case 'r': mostrarRelatorio(); break;
    case 'h': mostrarAjuda(); break;
  }
}

void mostrarStatus() {
  Serial.println();
  Serial.println("----- STATUS DA ESTACAO -----");
  Serial.print("Geracao solar atual : "); Serial.print(lerSolarPercentual()); Serial.println(" %");
  Serial.print("Tarifa em vigor      : "); Serial.println(modoVerdeAtivo ? "VERDE (desconto solar)" : "NORMAL");
  Serial.print("Vagas ocupadas       : "); Serial.print(contarOcupadas()); Serial.println(" de 3");
  if (contarOcupadas() > 0) {
    for (int i = 0; i < 3; i++) {
      if (vagas[i].ocupada) {
        Serial.print("Potencia por veiculo : "); Serial.print(vagas[i].potenciaAtual, 2); Serial.println(" kW");
        break;
      }
    }
  }
}

void listarSessoesAtivas() {
  Serial.println();
  Serial.println("----- SESSOES ATIVAS -----");
  if (contarOcupadas() == 0) {
    Serial.println("Nenhuma sessao ativa no momento.");
    return;
  }
  for (int i = 0; i < 3; i++) {
    if (vagas[i].ocupada) {
      unsigned long decorridoS = (millis() - vagas[i].inicio) / 1000;
      Serial.print("Vaga "); Serial.print(i + 1);
      Serial.print(" | tempo: "); Serial.print(decorridoS); Serial.print("s");
      Serial.print(" | potencia: "); Serial.print(vagas[i].potenciaAtual, 2); Serial.print("kW");
      Serial.print(" | tarifa: "); Serial.println(vagas[i].tarifaVerde ? "VERDE" : "NORMAL");
    }
  }
}

void mostrarRelatorio() {
  Serial.println();
  Serial.println("========= RELATORIO CONSOLIDADO =========");
  Serial.print("Sessoes concluidas total : "); Serial.println(sessoesTotal);
  Serial.print("  - com tarifa verde     : "); Serial.println(sessoesVerde);
  Serial.print("Energia total entregue   : "); Serial.print(energiaTotalKwh, 4); Serial.println(" kWh");
  Serial.print("Receita total            : R$ "); Serial.println(receitaTotal, 2);
  Serial.print("Economia gerada (verde)  : R$ "); Serial.println(economiaTotal, 2);

  if (historicoCount > 0) {
    Serial.println();
    Serial.println("Historico das ultimas sessoes:");
    int idx = historicoProximo;
    for (int n = 0; n < historicoCount; n++) {
      idx = (idx - 1 + 10) % 10;
      Sessao s = historico[idx];
      Serial.print("  Vaga "); Serial.print(s.vaga);
      Serial.print(" | "); Serial.print(s.duracaoS); Serial.print("s");
      Serial.print(" | "); Serial.print(s.energiaKwh, 4); Serial.print("kWh");
      Serial.print(" | R$ "); Serial.print(s.valor, 2);
      Serial.println(s.tarifaVerde ? " | VERDE" : " | NORMAL");
    }
  }
}

void mostrarAjuda() {
  Serial.println();
  Serial.println("===== Estacao LightWeek - comandos =====");
  Serial.println("1 / 2 / 3  -> liga ou desliga a sessao na vaga correspondente");
  Serial.println("+          -> inicia sessao na primeira vaga livre");
  Serial.println("-          -> encerra a sessao ativa mais antiga");
  Serial.println("s          -> mostra o status atual");
  Serial.println("l          -> lista as sessoes ativas");
  Serial.println("r          -> gera o relatorio consolidado");
  Serial.println("h          -> mostra este menu");
  Serial.println("==========================================");
}
