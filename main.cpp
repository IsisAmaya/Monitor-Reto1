#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <semaphore.h>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include "ClienteChat.h"
#include "ServidorChat.h"

// Semáforos para sincronizar acceso a la cola
sem_t emptySlots;  // Semáforo para contar los espacios vacíos en la cola
sem_t filledSlots;  // Semáforo para contar los espacios llenos en la cola

// Mutex para proteger el acceso a la cola
std::mutex queueMutex;

// Cola compartida
std::queue<std::string> messageQueue;

// Almacenar mensajes para el monitor
std::vector<std::string> producedMessages;
std::vector<std::string> consumedMessages;

// Variable de condición y mutex para controlar el estado
std::condition_variable cv;
std::mutex cv_m;
bool showStatus = false;
bool stopThreads = false;

/**
 * @brief Función para producir mensajes y agregarlos a la cola.
 * 
 * @param id Identificador del productor.
 */
void producer(int id) {
    int messageCount = 0;
    while (!stopThreads) {
        std::string message = "Mensaje del productor " + std::to_string(id) + " - " + std::to_string(messageCount++);

        // Espera hasta que haya espacio en la cola
        sem_wait(&emptySlots);

        // Bloquea el mutex para agregar el mensaje a la cola
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            messageQueue.push(message);
            producedMessages.push_back(message);  // Almacena el mensaje producido
        }

        // Señala que hay un mensaje disponible en la cola
        sem_post(&filledSlots);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));  // Simula tiempo de producción
    }
}

/**
 * @brief Función para consumir mensajes de la cola.
 * 
 * @param id Identificador del consumidor.
 * @param cliente Referencia al objeto ClienteChat para manejar el comando.
 */
void consumer(int id, ClienteChat& cliente) {
    while (!stopThreads) {
        // Espera hasta que haya un mensaje en la cola
        sem_wait(&filledSlots);

        // Bloquea el mutex para sacar el mensaje de la cola
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (!messageQueue.empty()) {
                std::string message = messageQueue.front();
                messageQueue.pop();
                consumedMessages.push_back(message);  // Almacena el mensaje consumido

                // Envía el mensaje al servidor
                cliente.manejarComando(message);
            }
        }

        // Señala que hay espacio disponible en la cola
        sem_post(&emptySlots);

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // Simula tiempo de consumo
    }
}

/**
 * @brief Función para el monitor que muestra mensajes producidos y consumidos.
 */
void mostrarEstado(ClienteChat& cliente) {
    showStatus = true;
    stopThreads = false;

    // Crear y lanzar hilos de productores y consumidores para la demostración
    std::vector<std::thread> producerThreads;
    std::vector<std::thread> consumerThreads;

    // Crea y lanza los hilos de productores
    for (int i = 0; i < 3; ++i) {
        producerThreads.emplace_back(producer, i);
    }

    // Crea y lanza los hilos de consumidores
    for (int i = 0; i < 2; ++i) {
        consumerThreads.emplace_back(consumer, i, std::ref(cliente));
    }

    std::this_thread::sleep_for(std::chrono::seconds(4));

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        std::cout << "\n--- Estado de Producción y Consumo ---\n";
        std::cout << "Producidos: \n";
        for (const auto& msg : producedMessages) {
            std::cout << msg << "\n";
        }
        std::cout << "Consumidos: \n";
        for (const auto& msg : consumedMessages) {
            std::cout << msg << "\n";
        }
        std::cout << "--- Fin del Estado ---\n\n";
    }

    for (int i = 0; i < 10; ++i) {  // Intentar liberar los semáforos varias veces
        sem_post(&filledSlots);
        sem_post(&emptySlots);
    }

    stopThreads = true;

    // Terminar hilos de productores y consumidores
    for (auto& thread : producerThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    for (auto& thread : consumerThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    std::cout << "Regresando al chat...\n";
    showStatus = false;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Uso: " << argv[0] << " <modo> [<direccionIP> <puerto>]\n";
        std::cerr << "Modos disponibles: servidor, cliente\n";
        return 1;
    }

    std::string modo = argv[1];

    if (modo == "servidor") {
        if (argc < 3) {
            std::cerr << "Uso: " << argv[0] << " servidor <puerto>\n";
            return 1;
        }
        int puerto = std::stoi(argv[2]);
        ServidorChat servidor(puerto);  // Inicializa el servidor con el puerto proporcionado
        servidor.iniciar();  // Inicia el servidor
    } else if (modo == "cliente") {
        if (argc < 4) {
            std::cerr << "Uso: " << argv[0] << " cliente <direccionIP> <puerto>\n";
            return 1;
        }
        std::string direccionIP = argv[2];
        int puerto = std::stoi(argv[3]);
        ClienteChat cliente(direccionIP, puerto);  // Inicializa el cliente con la dirección IP y puerto proporcionados
        cliente.conectarAlServidor();  // Conecta al servidor

        // Inicializa los semáforos
        const int queueSize = 10;
        sem_init(&emptySlots, 0, queueSize);
        sem_init(&filledSlots, 0, 0);

        // Bucle para manejar comandos del cliente
        std::string mensaje;
        while (std::getline(std::cin, mensaje)) {
            if (mensaje == "*mostrar proceso*") {
                mostrarEstado(cliente);
            } else {
                cliente.manejarComando(mensaje);  // Envía el mensaje al servidor
            }
        }

        // Destruye los semáforos después de su uso
        sem_destroy(&emptySlots);
        sem_destroy(&filledSlots);

        cliente.desconectar();  // Desconecta del servidor
    } else {
        std::cerr << "Modo desconocido: " << modo << "\n";
        return 1;
    }

    return 0;
}
