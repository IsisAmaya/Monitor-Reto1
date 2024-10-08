#include "MonitorServidores.h"
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <system_error>
#include <csignal>
#include <unistd.h>// Necesario para usar access()
// Incluir las cabeceras necesarias para sockets
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <memory>
#include <cstring>
#include <sstream>
#include <queue>
#include <mutex>

// Cola para almacenar mensajes y mutex para sincronización
std::queue<std::string> message_queue;
std::mutex queue_mutex;

std::vector<std::shared_ptr<std::atomic<bool>>> server_active;

// Especifica la dirección IP que deseas usar
const char* ip_address = "172.18.76.218"; // Cambia esta IP según tus necesidades

// Comprueba si el puerto está disponible
bool is_port_available(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "Error al crear un socket temporal para verificar el puerto.\n";
        return false;
    }

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    // Reemplaza INADDR_ANY con la IP específica
    if (inet_pton(AF_INET, ip_address, &address.sin_addr) <= 0) {
        std::cerr << "Error al convertir la dirección IP: " << ip_address << std::endl;
        close(sockfd);
        return false;
    }

    bool available = (bind(sockfd, (sockaddr*)&address, sizeof(address)) == 0);
    close(sockfd);
    return available;
}

// Inicia un servidor en el puerto especificado
void start_server(int server_id, int port, std::shared_ptr<std::atomic<bool>> server_active) {
    std::cout << "Iniciando hilo para Servidor " << server_id << " en el puerto " << port << std::endl;

    std::string command = "./build/chat servidor " + std::to_string(port);

    // Verificar si el archivo existe antes de ejecutar el comando
    if (access("./build/chat", F_OK) == -1) {
        std::cerr << "El archivo ./build/chat no existe o no es accesible." << std::endl;
        return;
    }

    while (true) {
        if (*server_active) {
            if (is_port_available(port)) {
                std::cout << "Iniciando Servidor " << server_id << " en puerto " << port << std::endl;
                int result = std::system(command.c_str());
                if (result != 0) {
                    std::cerr << "Servidor " << server_id << " se ha detenido con error " << result << " (código de salida: " << WEXITSTATUS(result) << ")" << std::endl;
                    *server_active = false;

                    // Intentar de nuevo después de un breve retraso
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }
            } else {
                std::cerr << "El puerto " << port << " no está disponible. Intentando nuevamente en 5 segundos...\n";
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        } else {
            // Esperar un poco antes de volver a verificar si se debe reiniciar
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    std::cout << "Hilo para Servidor " << server_id << " terminado" << std::endl;
}

// Monitorea los servidores y los reinicia si es necesario
void monitor_servers() {
    while (true) {
        for (size_t i = 0; i < server_active.size(); ++i) {
            std::cout << "Monitoreando servidor " << i + 1 << std::endl;
            if (!*server_active[i]) {
                std::cout << "Reiniciando Servidor " << i + 1 << "...\n";
                *server_active[i] = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(5)); // Monitoreo cada 5 segundos
    }
}

// Recibe mensajes de los servidores y los almacena en la cola
void recibirInformacionServidor() {
    int descriptorMonitor = socket(AF_INET, SOCK_DGRAM, 0);
    if (descriptorMonitor == -1) {
        std::cerr << "Error al crear el socket para recibir.\n";
        return;
    }

    sockaddr_in direccionMonitor;
    direccionMonitor.sin_family = AF_INET;
    direccionMonitor.sin_port = htons(55555); // Puerto para recibir los datos

    // Reemplaza INADDR_ANY con la IP específica
    if (inet_pton(AF_INET, ip_address, &direccionMonitor.sin_addr) <= 0) {
        std::cerr << "Error al convertir la dirección IP: " << ip_address << std::endl;
        close(descriptorMonitor);
        return;
    }

    if (bind(descriptorMonitor, (sockaddr*)&direccionMonitor, sizeof(direccionMonitor)) == -1) {
        std::cerr << "Error al hacer bind del socket del monitor.\n";
        close(descriptorMonitor);
        return;
    }

    char buffer[1024];
    sockaddr_in emisorDireccion;
    socklen_t emisorTamano = sizeof(emisorDireccion);

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytesRecibidos = recvfrom(descriptorMonitor, buffer, 1024, 0, (sockaddr*)&emisorDireccion, &emisorTamano);
        if (bytesRecibidos > 0) {
            // Null-terminar el buffer recibido
            buffer[bytesRecibidos] = '\0';

            // Convertir el buffer a std::string
            std::string received_data(buffer);

            // Separar el string en un array de strings
            std::vector<std::string> messages;
            std::istringstream stream(received_data);
            std::string token;
            while (std::getline(stream, token, '\n')) {  // Usa el delimitador '\n'
                if (!token.empty()) {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    message_queue.push(token);
                }
            }
        }
    }
    close(descriptorMonitor);
}

// Muestra la información almacenada en la cola cada 7 segundos
void mostrarInformacionServidor() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(7));
        std::lock_guard<std::mutex> lock(queue_mutex);

        while (!message_queue.empty()) {
            std::cout << message_queue.front() << std::endl;
            message_queue.pop();
        }
    }
}

// Función principal
int main(int argc, char* argv[]) {
    if (argc < 3 || (argc - 1) % 2 != 0) {
        std::cerr << "Uso: " << argv[0] << " <num_servidores> <puerto1> ... <puertoN>\n";
        return 1;
    }

    int num_servers = std::stoi(argv[1]);
    if (num_servers <= 0 || argc != 2 + num_servers) {
        std::cerr << "Número de servidores inválido o número incorrecto de puertos.\n";
        return 1;
    }

    std::vector<int> ports;
    for (int i = 2; i < argc; ++i) {
        ports.push_back(std::stoi(argv[i]));
    }

    server_active.resize(num_servers);

    // Inicializar el vector de std::shared_ptr<std::atomic<bool>>
    for (int i = 0; i < num_servers; ++i) {
        server_active[i] = std::make_shared<std::atomic<bool>>(true);
    }

    std::vector<std::thread> server_threads;

    // Iniciar servidores en los puertos especificados usando lambda functions
    for (int i = 0; i < num_servers; ++i) {
        server_threads.emplace_back([i, &ports]() {
            start_server(i + 1, ports[i], server_active[i]);
        });
    }

    // Iniciar monitoreo
    std::thread monitor_thread(monitor_servers);

    // Iniciar recepción de información de servidores y mostrar información
    std::thread recibirHilo(recibirInformacionServidor);
    std::thread mostrarHilo(mostrarInformacionServidor);

    // Esperar a que los hilos terminen
    for (auto& t : server_threads) {
        t.join();
    }
    monitor_thread.join();
    recibirHilo.join();
    mostrarHilo.join();

    return 0;
}
