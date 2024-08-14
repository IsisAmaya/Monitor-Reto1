#include "ServidorChat.h"
#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <thread>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <chrono>
#include <map>
// Definir los códigos de escape para diferentes colores
#define RESET   "\033[0m"
#define RED     "\033[31m"      /* Red */
#define GREEN   "\033[32m"      /* Green */
#define YELLOW  "\033[33m"      /* Yellow */
#define BLUE    "\033[34m"      /* Blue */
#define MAGENTA "\033[35m"      /* Magenta */
#define CYAN    "\033[36m"      /* Cyan */
#define WHITE   "\033[37m"      /* White */


// Constructor que inicializa el puerto del servidor
ServidorChat::ServidorChat(int puerto)
    : puerto(puerto), descriptorServidor(-1), totalMensajes(0) {
    tiempoInicio = std::chrono::steady_clock::now();
}

// Método para iniciar el servidor
void ServidorChat::iniciar() {
    // Crear el socket del servidor
    descriptorServidor = ::socket(AF_INET, SOCK_STREAM, 0);
    if (descriptorServidor == -1) {
        std::cerr << "Error al crear el socket del servidor.\n";
        return;
    }

    // Configurar SO_REUSEADDR y SO_REUSEPORT
    int opt = 1;
    if (setsockopt(descriptorServidor, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) == -1) {
        std::cerr << "Error al configurar el socket con SO_REUSEADDR | SO_REUSEPORT.\n";
        close(descriptorServidor);
        return;
    }

    sockaddr_in direccionServidor;
    direccionServidor.sin_family = AF_INET;
    direccionServidor.sin_port = htons(puerto);
    direccionServidor.sin_addr.s_addr = INADDR_ANY;

    // Asociar el socket a la dirección y puerto
    if (bind(descriptorServidor, (sockaddr*)&direccionServidor, sizeof(direccionServidor)) == -1) {
        std::cerr << "Error al hacer bind del socket del servidor.\n";
        return;
    }

    // Poner el servidor en modo escucha
    if (listen(descriptorServidor, 10) == -1) {
        std::cerr << "Error al poner el servidor en modo escucha.\n";
        return;
    }

    std::cout << "Servidor iniciado en el puerto " << puerto << ". Esperando conexiones...\n";

    // Crear hilo para enviar información al monitor
    std::thread([this]() {
        while (true) {
            enviarInformacionMonitor();
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }).detach();    

    // Aceptar conexiones entrantes
    while (true) {
        sockaddr_in direccionCliente;
        socklen_t tamanoDireccionCliente = sizeof(direccionCliente);
        int descriptorCliente = accept(descriptorServidor, (sockaddr*)&direccionCliente, &tamanoDireccionCliente);

        if (descriptorCliente == -1) {
            std::cerr << "Error al aceptar la conexión de un cliente.\n";
            continue;
        }

        // Crear un hilo para manejar el cliente
        std::thread hiloCliente(&ServidorChat::manejarCliente, this, descriptorCliente);
        hiloCliente.detach();
    }
}

// Manejar la comunicación con un cliente
void ServidorChat::manejarCliente(int descriptorCliente) {
    char buffer[1024];
    std::string nombreUsuario;

    // Solicitar el nombre del usuario
    send(descriptorCliente, "Ingrese su nombre: ", 20, 0);
    ssize_t bytesRecibidos = recv(descriptorCliente, buffer, 1024, 0);
    if (bytesRecibidos <= 0) {
        close(descriptorCliente);
        return;
    }

    nombreUsuario = std::string(buffer, bytesRecibidos);
    nombreUsuario.erase(nombreUsuario.find_last_not_of(" \n\r\t") + 1); // Eliminar espacios en blanco

    {
        std::lock_guard<std::mutex> lock(mutexUsuarios);
        usuarios.emplace_back(nombreUsuario, descriptorCliente);
    }

    // Notificar a todos los usuarios que un nuevo usuario se ha conectado
    std::string mensajeBienvenida = nombreUsuario + " se ha conectado al chat.\n";
    enviarMensajeATodos(mensajeBienvenida, descriptorCliente);

    // Actualizar tiempos
    tiemposUltimosMensajes[descriptorCliente] = std::chrono::steady_clock::now();

    // Manejar los mensajes del cliente
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        bytesRecibidos = recv(descriptorCliente, buffer, 1024, 0);

        if (bytesRecibidos <= 0) {
            // El cliente se ha desconectado
            {
                std::lock_guard<std::mutex> lock(mutexUsuarios);
                for (auto it = usuarios.begin(); it != usuarios.end(); ++it) {
                    if (it->obtenerDescriptorSocket() == descriptorCliente) {
                        std::string mensajeDespedida = it->obtenerNombreUsuario() + " se ha desconectado del chat.\n";
                        enviarMensajeATodos(mensajeDespedida, descriptorCliente);
                        usuarios.erase(it);
                        break;
                    }
                }
            }
            close(descriptorCliente);
            break;
        }

        // Actualizar métricas
        {
            std::lock_guard<std::mutex> lock(mutexUsuarios);
            totalMensajes++;
            auto ahora = std::chrono::steady_clock::now();
            auto it = tiemposUltimosMensajes.find(descriptorCliente);
            if (it != tiemposUltimosMensajes.end()) {
                auto tiempoUltimoMensaje = it->second;
                std::chrono::duration<double> tiempoEntreMensajes = ahora - tiempoUltimoMensaje;
                // Guardar tiempo entre mensajes
                it->second = ahora;
            }
        }

        std::string mensaje = std::string(buffer, bytesRecibidos);

        // Procesar comandos del protocolo
        if (mensaje.substr(0, 9) == "@usuarios") {
            enviarListaUsuarios(descriptorCliente);
        } else if (mensaje.substr(0, 9) == "@conexion") {
            enviarDetallesConexion(descriptorCliente);
        } else if (mensaje.substr(0, 6) == "@salir") {
            close(descriptorCliente);
            break;
        } else if (mensaje.substr(0, 2) == "@h") {
            std::string ayuda = "Comandos disponibles:\n"
                                "@usuarios - Lista de usuarios conectados\n"
                                "@conexion - Muestra la conexión y el número de usuarios\n"
                                "@salir - Desconectar del chat\n";
            send(descriptorCliente, ayuda.c_str(), ayuda.size(), 0);
        } else {
            // Enviar el mensaje a todos los usuarios
            mensaje = nombreUsuario + ": " + mensaje;
            enviarMensajeATodos(mensaje, descriptorCliente);
        }
    }
}

// Enviar un mensaje a todos los usuarios conectados, excepto al remitente
void ServidorChat::enviarMensajeATodos(const std::string& mensaje, int descriptorRemitente) {
    std::lock_guard<std::mutex> lock(mutexUsuarios);
    for (const auto& usuario : usuarios) {
        if (usuario.obtenerDescriptorSocket() != descriptorRemitente) {
            send(usuario.obtenerDescriptorSocket(), mensaje.c_str(), mensaje.size(), 0);
        }
    }
}

// Enviar la lista de usuarios conectados al cliente especificado
void ServidorChat::enviarListaUsuarios(int descriptorCliente) {
    std::lock_guard<std::mutex> lock(mutexUsuarios);
    std::string listaUsuarios = "Usuarios conectados:\n";
    for (const auto& usuario : usuarios) {
        listaUsuarios += usuario.obtenerNombreUsuario() + "\n";
    }
    send(descriptorCliente, listaUsuarios.c_str(), listaUsuarios.size(), 0);
}

// Enviar los detalles de la conexión y el número de usuarios conectados
void ServidorChat::enviarDetallesConexion(int descriptorCliente) {
    std::lock_guard<std::mutex> lock(mutexUsuarios);
    std::string detalles = "Número de usuarios conectados: " + std::to_string(usuarios.size()) + "\n";
    send(descriptorCliente, detalles.c_str(), detalles.size(), 0);
}

// Enviar el promedio de mensajes al monitor
std::string ServidorChat::enviarPromedioMensajes() {
    std::chrono::duration<double> duracion = std::chrono::steady_clock::now() - tiempoInicio;
    double promedioMensajes = totalMensajes / duracion.count();
    std::string mensaje = "Promedio de mensajes: " + std::to_string(promedioMensajes) + " mensajes/segundo\n";
    return mensaje;
}

// Enviar la tasa de uso al monitor
std::string ServidorChat::enviarTasaUso() {
    std::chrono::duration<double> duracion = std::chrono::steady_clock::now() - tiempoInicio;
    double tasaUso = totalMensajes / duracion.count();
    std::string mensaje = "Tasa de uso: " + std::to_string(tasaUso) + " mensajes/segundo\n";
    return mensaje;
}

// Enviar el tiempo promedio entre mensajes al monitor
std::string ServidorChat::enviarTiempoEntreMensajes() {
    double tiempoTotal = 0.0;
    int contador = 0;
    auto ahora = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(mutexUsuarios);
    for (const auto& it : tiemposUltimosMensajes) {
        auto tiempoUltimoMensaje = it.second;
        if (tiempoUltimoMensaje != ahora) {
            std::chrono::duration<double> tiempoEntreMensajes = ahora - tiempoUltimoMensaje;
            tiempoTotal += tiempoEntreMensajes.count();
            contador++;
        }
    }
    double tiempoPromedio = (contador > 0) ? (tiempoTotal / contador) : 0.0;
    std::string mensaje = "Tiempo promedio entre mensajes: " + std::to_string(tiempoPromedio) + " segundos\n";
    return mensaje;
}

// Enviar el tiempo de actividad al monitor
std::string ServidorChat::enviarTiempoActividad() {
    std::chrono::duration<double> duracion = std::chrono::steady_clock::now() - tiempoInicio;
    std::string mensaje = "Tiempo de actividad: " + std::to_string(duracion.count()) + " segundos\n";
    return mensaje;
}


// Enviar el número de usuarios conectados al monitor
std::string ServidorChat::enviarNumeroUsuarios() {
    std::lock_guard<std::mutex> lock(mutexUsuarios);
    std::string mensaje = "Número de usuarios conectados: " + std::to_string(usuarios.size()) + "\n";
    return mensaje;
}


// Función para concatenar múltiples strings con un delimitador
std::string ServidorChat::concatenarMensajes(const std::vector<std::string>& mensajes, const std::string& delimiter) {
    std::string mensajesConcatenados;
    
    for (const auto& msg : mensajes) {
        mensajesConcatenados += msg + delimiter;
    }
    
    return mensajesConcatenados;
}

// Enviar toda la información al monitor
void ServidorChat::enviarInformacionMonitor() {
    int socketDescriptor = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socketDescriptor == -1) {
        std::cerr << "Error al crear el socket UDP.\n";
        return;
    }

    sockaddr_in direccionMonitor;
    direccionMonitor.sin_family = AF_INET;
    direccionMonitor.sin_port = htons(55555); // Puerto para el monitor
    direccionMonitor.sin_addr.s_addr = inet_addr("127.0.0.1"); // Dirección IP del monitor (localhost)

    std::string mensaje = GREEN "Servidor en el puerto: " + std::to_string(puerto) + RESET + "\n";
    std::string numeroDeUsuarios = enviarNumeroUsuarios();
    std::string tasaDeUso = enviarTasaUso();
    std::string promedioMensajes = enviarPromedioMensajes(); 
    std::string tiempoEntreMensajes = enviarTiempoEntreMensajes();
    std::string tiempoDeActividad = enviarTiempoActividad();
    
    std::vector<std::string> messages = {mensaje, numeroDeUsuarios, tasaDeUso, promedioMensajes, tiempoEntreMensajes, tiempoDeActividad};
    std::string mensajeFinal = concatenarMensajes(messages);
    sendto(socketDescriptor, mensajeFinal.c_str(), mensajeFinal.size(), 0, (struct sockaddr*)&direccionMonitor, sizeof(direccionMonitor));

    //enviarPromedioMensajes(socketDescriptor, direccionMonitor);
    //enviarTasaUso(socketDescriptor, direccionMonitor);
    //enviarTiempoEntreMensajes(socketDescriptor, direccionMonitor);
    //enviarTiempoActividad(socketDescriptor, direccionMonitor);
    //enviarNumeroUsuarios(socketDescriptor, direccionMonitor);  // Enviar número de usuarios conectados

    close(socketDescriptor);
}