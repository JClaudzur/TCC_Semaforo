import cv2
import numpy as np
from ultralytics import YOLO
import time
from collections import deque
import threading
import urllib.request
import json
import random

# ===========================================
# CONFIGURAÇÕES
# ===========================================
ESP32_IP = ""  # ← MUDE PARA SEU IP!
ESP32_SEMAFORO_IP = ""  # ← MUDE PARA O IP DO SEU ESP32 DO SEMÁFORO

STREAM_URL = f"http://{ESP32_IP}/stream"

# Configuração YOLO
CONFIANCA_MINIMA = 0.3  # Ajuste conforme necessário
CLASSES_VEICULOS = {
    2: 'carro',
    3: 'moto',
    5: 'onibus',
    7: 'caminhao'
}

# Estatísticas (CORRIGIDO: adicionado veiculos_detectados)
stats = {
    'total_frames': 0,
    'frames_com_carros': 0,
    'max_carros': 0,
    'veiculos_detectados': 0,  # ← ADICIONADO
    'inicio': time.time(),
    'fps_historico': deque(maxlen=30)
}

print("="*60)
print("🎬 YOLO COM STREAM MJPEG - ESP32-CAM")
print("="*60)
print(f"\n📡 Stream: {STREAM_URL}")
print(f"🎯 Confiança mínima: {CONFIANCA_MINIMA*100}%")
print("\n💡 Controles:")
print("   [Q] - Sair")
print("   [S] - Screenshot")
print("   [+] - Aumentar confiança")
print("   [-] - Diminuir confiança")
print("\n🚀 Carregando YOLO...")

# Carrega modelo
modelo = YOLO('yolov8n.pt')
print("✅ YOLO carregado!\n")
print("="*60)
print("🎬 Conectando ao stream...\n")

# ===========================================
# CLASSE PARA LER STREAM MJPEG
# ===========================================
class MJPEGStreamReader:
    """
    Lê stream MJPEG da ESP32-CAM
    Muito mais eficiente que requests.get() repetidos!
    """
    
    def __init__(self, url):
        self.url = url
        self.stream = None
        self.bytes_buffer = b''
        self.current_frame = None
        self.lock = threading.Lock()
        self.running = False
        self.error_count = 0
        
    def connect(self):
        """Conecta ao stream"""
        try:
            self.stream = urllib.request.urlopen(self.url, timeout=10)
            self.running = True
            print("✅ Conectado ao stream!")
            return True
        except Exception as e:
            print(f"❌ Erro ao conectar: {e}")
            return False
    
    def read_frame(self):
        """Lê um frame do stream"""
        if not self.stream:
            return None
        
        try:
            # Lê até encontrar início de JPEG
            while True:
                self.bytes_buffer += self.stream.read(1024)
                
                # Procura marcadores JPEG
                a = self.bytes_buffer.find(b'\xff\xd8')  # Início JPEG
                b = self.bytes_buffer.find(b'\xff\xd9')  # Fim JPEG
                
                if a != -1 and b != -1:
                    # Encontrou um frame completo!
                    jpg = self.bytes_buffer[a:b+2]
                    self.bytes_buffer = self.bytes_buffer[b+2:]
                    
                    # Decodifica JPEG
                    frame = cv2.imdecode(
                        np.frombuffer(jpg, dtype=np.uint8),
                        cv2.IMREAD_COLOR
                    )
                    
                    if frame is not None:
                        with self.lock:
                            self.current_frame = frame
                        self.error_count = 0
                        return frame
                    
        except Exception as e:
            self.error_count += 1
            if self.error_count % 10 == 1:
                print(f"⚠️  Erro ao ler frame: {e}")
            return None
    
    def get_frame(self):
        """Retorna último frame capturado"""
        with self.lock:
            # CORRIGIDO: verifica se current_frame não é None antes de copiar
            if self.current_frame is not None:
                return self.current_frame.copy()
            return None
    
    def stop(self):
        """Para o stream"""
        self.running = False
        if self.stream:
            try:
                self.stream.close()
            except:
                pass

# ===========================================
# THREAD PARA LER STREAM CONTINUAMENTE
# ===========================================
def stream_reader_thread(reader):
    """
    Thread que fica lendo o stream continuamente
    Isso garante que sempre temos frames atualizados
    """
    print("🎬 Thread de leitura iniciada")
    frame_count = 0
    
    while reader.running:
        frame = reader.read_frame()
        if frame is not None:
            frame_count += 1
            if frame_count % 30 == 0:  # Log a cada 30 frames
                print(f"📹 Stream: {frame_count} frames lidos")
    
    print("🛑 Thread de leitura finalizada")

# ===========================================
# FUNÇÃO DE DETECÇÃO
# ===========================================
def detectar_veiculos(frame):
    """Detecta veículos no frame"""
    if frame is None:
        return 0, None, []
    
    # YOLO detecta
    resultados = modelo(frame, conf=CONFIANCA_MINIMA, verbose=False)
    
    # Processa detecções
    veiculos = []
    for box in resultados[0].boxes:
        classe = int(box.cls[0])
        conf = float(box.conf[0])
        
        if classe in CLASSES_VEICULOS:
            x1, y1, x2, y2 = box.xyxy[0].cpu().numpy()
            veiculos.append({
                'tipo': CLASSES_VEICULOS[classe],
                'confianca': conf,
                'bbox': (int(x1), int(y1), int(x2), int(y2))
            })
    
    # CORRIGIDO: incrementa contador total de veículos
    stats['veiculos_detectados'] += len(veiculos)
    
    # Desenha anotações
    frame_anotado = frame.copy()
    altura, largura = frame_anotado.shape[:2]
    
    # Desenha cada veículo
    for i, veiculo in enumerate(veiculos):
        x1, y1, x2, y2 = veiculo['bbox']
        tipo = veiculo['tipo']
        conf = veiculo['confianca']
        
        # Cor por tipo
        cores = {
            'carro': (0, 255, 0),
            'moto': (255, 0, 255),
            'onibus': (0, 165, 255),
            'caminhao': (0, 255, 255)
        }
        cor = cores.get(tipo, (0, 255, 0))
        
        # Retângulo
        cv2.rectangle(frame_anotado, (x1, y1), (x2, y2), cor, 2)
        
        # Label
        texto = f'{tipo} {conf*100:.0f}%'
        (tw, th), _ = cv2.getTextSize(texto, cv2.FONT_HERSHEY_SIMPLEX, 0.6, 2)
        cv2.rectangle(frame_anotado, (x1, y1-th-10), (x1+tw+10, y1), cor, -1)
        cv2.putText(frame_anotado, texto, (x1+5, y1-5),
                   cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 0, 0), 2)
        
        # Número
        cv2.putText(frame_anotado, f'#{i+1}', (x1+5, y2-10),
                   cv2.FONT_HERSHEY_SIMPLEX, 0.7, cor, 2)
    
    # Interface no topo
    overlay = frame_anotado.copy()
    cv2.rectangle(overlay, (0, 0), (largura, 90), (0, 0, 0), -1)
    cv2.addWeighted(overlay, 0.7, frame_anotado, 0.3, 0, frame_anotado)
    
    # Contagem
    cor_texto = (0, 255, 0) if len(veiculos) > 0 else (100, 100, 100)
    cv2.putText(frame_anotado, f'{len(veiculos)} VEICULOS', (10, 40),
                cv2.FONT_HERSHEY_SIMPLEX, 1.2, cor_texto, 3)
    
    # FPS
    if len(stats['fps_historico']) > 0:
        fps_medio = np.mean(stats['fps_historico'])
        cv2.putText(frame_anotado, f'FPS: {fps_medio:.1f}', (10, 70),
                   cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 0), 2)
    
    # Confiança
    cv2.putText(frame_anotado, f'Conf: {CONFIANCA_MINIMA*100:.0f}%', 
               (largura-150, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)
    
    # Stream indicator
    cv2.circle(frame_anotado, (largura-30, 70), 8, (0, 255, 0), -1)
    cv2.putText(frame_anotado, 'STREAM', (largura-110, 75),
               cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
    
    return len(veiculos), frame_anotado, veiculos

# ===========================================
# COMUNICAÇÃO COM ESP32 DO SEMÁFORO
# ===========================================
def simular_via_b(quantidade_via_a):
    """
    Simula via B de forma inteligente
    Inversamente proporcional: se A tem muitos carros, B tem poucos
    """
    if quantidade_via_a > 7:
        # Via A congestionada → Via B livre
        return random.randint(0, 3)
    elif quantidade_via_a > 3:
        # Via A moderada → Via B moderada
        return random.randint(2, 6)
    else:
        # Via A livre → Via B pode ter mais
        return random.randint(3, 8)

def enviar_contagem_semaforo(via_a, via_b):
    """
    Envia quantidade de veículos para o ESP32 via POST JSON
    """
    try:
        # Monta JSON
        dados = json.dumps({"via_a": via_a, "via_b": via_b})
        
        # Cria requisição POST
        url = f"http://{ESP32_SEMAFORO_IP}/update"
        req = urllib.request.Request(
            url,
            data=dados.encode('utf-8'),
            headers={'Content-Type': 'application/json'},
            method='POST'
        )
        
        # Envia
        urllib.request.urlopen(req, timeout=1)
        
    except Exception as e:
        pass  # Ignora erros para não travar o stream

def enviar_contagem_periodica(quantidade):
    """Envia contagem a cada 2 segundos (evita flood)"""
    tempo_atual = time.time()
    if not hasattr(enviar_contagem_periodica, 'ultimo_envio'):
        enviar_contagem_periodica.ultimo_envio = 0
    
    if tempo_atual - enviar_contagem_periodica.ultimo_envio >= 2:
        enviar_contagem_periodica.ultimo_envio = tempo_atual
        
        # ⭐ CORRIGIDO: Simula via B antes de enviar
        via_b = simular_via_b(quantidade)
        
        # Envia ambos os valores
        threading.Thread(
            target=enviar_contagem_semaforo, 
            args=(quantidade, via_b),  # ← AGORA ENVIA 2 ARGUMENTOS!
            daemon=True
        ).start()
# ===========================================
# LOOP PRINCIPAL
# ===========================================
def main():
    global CONFIANCA_MINIMA
    
    # Cria leitor de stream
    reader = MJPEGStreamReader(STREAM_URL)
    
    # Conecta ao stream
    if not reader.connect():
        print("\n❌ Não foi possível conectar ao stream!")
        print("💡 Verifique:")
        print("   1. IP está correto?")
        print("   2. ESP32-CAM está ligada?")
        print("   3. Acesse no navegador: " + STREAM_URL)
        return
    
    # Inicia thread de leitura
    thread = threading.Thread(target=stream_reader_thread, args=(reader,), daemon=True)
    thread.start()
    
    # Aguarda primeiro frame
    print("⏳ Aguardando primeiro frame...")
    timeout = time.time() + 10  # 10 segundos de timeout
    while reader.get_frame() is None:
        if time.time() > timeout:
            print("❌ Timeout ao aguardar primeiro frame!")
            reader.stop()
            return
        time.sleep(0.1)
    
    print("✅ Primeiro frame recebido!")
    print("\n🎬 Iniciando detecção...\n")
    print("="*60 + "\n")
    
    ultimo_fps_log = time.time()
    
    try:
        while True:
            inicio_frame = time.time()
            
            # Pega frame mais recente do stream
            frame = reader.get_frame()
            
            if frame is not None:
                stats['total_frames'] += 1
                
                # Detecta veículos
                quantidade, frame_anotado, deteccoes = detectar_veiculos(frame)
                
                # Envia contagem para o semáforo
                enviar_contagem_periodica(quantidade)

                # Atualiza estatísticas
                if quantidade > 0:
                    stats['frames_com_carros'] += 1
                    if quantidade > stats['max_carros']:
                        stats['max_carros'] = quantidade
                
                # Calcula FPS
                tempo_frame = time.time() - inicio_frame
                if tempo_frame > 0:
                    fps = 1.0 / tempo_frame
                    stats['fps_historico'].append(fps)
                
                # Log no console (a cada 2 segundos)
                if time.time() - ultimo_fps_log > 2:
                    if quantidade > 0:
                        tipos = ', '.join([d['tipo'] for d in deteccoes])
                        print(f"🚗 {quantidade} veículos | {tipos}")
                    fps_medio = np.mean(stats['fps_historico']) if len(stats['fps_historico']) > 0 else 0
                    print(f"⚡ FPS: {fps_medio:.1f} | Total frames: {stats['total_frames']}")
                    ultimo_fps_log = time.time()
                
                # Mostra frame
                cv2.imshow('YOLO Stream - ESP32-CAM', frame_anotado)
            else:
                # CORRIGIDO: aguarda um pouco se não há frame disponível
                time.sleep(0.01)
            
            # Processa teclas
            tecla = cv2.waitKey(1) & 0xFF
            
            if tecla == ord('q') or tecla == ord('Q'):
                print("\n🛑 Encerrando...")
                break
            
            elif tecla == ord('s') or tecla == ord('S'):
                if frame is not None:
                    timestamp = time.strftime("%Y%m%d_%H%M%S")
                    filename = f'stream_screenshot_{timestamp}.jpg'
                    cv2.imwrite(filename, frame_anotado)
                    print(f"\n📸 Screenshot salvo: {filename}")
            
            elif tecla == ord('+') or tecla == ord('='):
                CONFIANCA_MINIMA = min(0.9, CONFIANCA_MINIMA + 0.05)
                print(f"\n🎯 Confiança: {CONFIANCA_MINIMA*100:.0f}%")
            
            elif tecla == ord('-') or tecla == ord('_'):
                CONFIANCA_MINIMA = max(0.1, CONFIANCA_MINIMA - 0.05)
                print(f"\n🎯 Confiança: {CONFIANCA_MINIMA*100:.0f}%")
    
    except KeyboardInterrupt:
        print("\n\n⚠️  Interrompido (Ctrl+C)")
    
    finally:
        # Limpa recursos
        reader.stop()
        cv2.destroyAllWindows()
        
        # Aguarda thread finalizar
        time.sleep(0.5)
        
        # Relatório final
        print("\n\n" + "="*60)
        print("📊 RELATÓRIO FINAL - STREAM")
        print("="*60)
        
        tempo_total = time.time() - stats['inicio']
        minutos = int(tempo_total // 60)
        segundos = int(tempo_total % 60)
        
        print(f"\n⏱️  Tempo total: {minutos}m {segundos}s")
        print(f"🎬 Frames processados: {stats['total_frames']}")
        print(f"🚗 Total de veículos detectados: {stats['veiculos_detectados']}")
        print(f"🎯 Máximo simultâneo: {stats['max_carros']} veículos")
        print(f"📊 Frames com detecções: {stats['frames_com_carros']}")
        
        if stats['total_frames'] > 0:
            fps_medio = stats['total_frames'] / tempo_total
            media_veiculos = stats['veiculos_detectados'] / stats['total_frames']
            percentual = (stats['frames_com_carros'] / stats['total_frames']) * 100
            print(f"⚡ FPS médio: {fps_medio:.2f}")
            print(f"📊 Média de veículos/frame: {media_veiculos:.2f}")
            print(f"📈 Taxa de detecção: {percentual:.1f}%")
        
        print("\n✅ Stream finalizado!")
        print("="*60)

# ===========================================
# EXECUÇÃO
# ===========================================
if __name__ == "__main__":
    main()