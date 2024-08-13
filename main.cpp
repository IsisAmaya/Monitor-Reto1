#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <semaphore.h>
#include <mutex>
#include "ClienteChat.h"
#include "ServidorChat.h"

// Semáforos para sincronizar acceso a la cola
sem_t emptySlots;  // Semáforo para contar los espacios vacíos en la cola
sem_t filledSlots;  // Semáforo para contar los espacios llenos en la cola

// Mutex para proteger el acceso a la cola
std::mutex queueMutex;

// Cola compartida
std::queue<std::string> messageQueue;

/**
 * @brief Función para producir mensajes y agregarlos a la cola.
 * 
 * @param id Identificador del productor.
 */
void producer(int id) {
    int messageCount = 0;
    while (true) {
        std::string message = "Mensaje del productor " + std::to_string(id) + " - " + std::to_string(messageCount++);

        // Espera hasta que haya espacio en la cola
        sem_wait(&emptySlots);

        // Bloquea el mutex para agregar el mensaje a la cola
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            messageQueue.push(message);
            std::cout << "Producido: " << message << std::endl;
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
    while (true) {
        // Espera hasta que haya un mensaje en la cola
        sem_wait(&filledSlots);

        // Bloquea el mutex para sacar el mensaje de la cola
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            std::string message = messageQueue.front();
            messageQueue.pop();
            std::cout << "Consumido por el consumidor " << id << ": " << message << std::endl;

            // Envía el mensaje al servidor
            cliente.manejarComando(message);
        }

        // Señala que hay espacio disponible en la cola
        sem_post(&emptySlots);

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // Simula tiempo de consumo
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Uso: " << argv[0] << " <modo> <direccionIP> <puerto>\n";
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

        // Vectores para almacenar los hilos de productores y consumidores
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

        // Bucle para manejar comandos del cliente
        std::string mensaje;
        while (std::getline(std::cin, mensaje)) {
            cliente.manejarComando(mensaje);  // Envía el mensaje al servidor
        }

        // Une los hilos de productores
        for (auto& thread : producerThreads) {
            thread.join();
        }

        // Une los hilos de consumidores
        for (auto& thread : consumerThreads) {
            thread.join();
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
