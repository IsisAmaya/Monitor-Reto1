#ifndef SERVIDORCHAT_H
#define SERVIDORCHAT_H

#include <vector>
#include <mutex>
#include <chrono>
#include <string>
#include <map>
#include <netinet/in.h>  // Para sockaddr_in

// Clase Usuario que debe definirse en otro lugar
class Usuario {
public:
    Usuario(const std::string& nombreUsuario, int descriptorSocket)
        : nombreUsuario(nombreUsuario), descriptorSocket(descriptorSocket) {}

    std::string obtenerNombreUsuario() const { return nombreUsuario; }
    int obtenerDescriptorSocket() const { return descriptorSocket; }

private:
    std::string nombreUsuario;
    int descriptorSocket;
};

class ServidorChat {
public:
    ServidorChat(int puerto);
    void iniciar();

private:
    void manejarCliente(int descriptorCliente);
    void enviarMensajeATodos(const std::string& mensaje, int descriptorRemitente);
    void enviarListaUsuarios(int descriptorCliente);
    void enviarDetallesConexion(int descriptorCliente);
    std::string enviarPromedioMensajes();
    std::string enviarTasaUso();
    std::string enviarTiempoEntreMensajes();
    std::string enviarTiempoActividad();
    std::string enviarNumeroUsuarios();  // Nueva función
    void enviarInformacionMonitor();

    std::string concatenarMensajes(const std::vector<std::string>& mensajes, const std::string& delimiter="\n");  // Nueva función
    
    int puerto;
    int descriptorServidor;
    std::chrono::steady_clock::time_point tiempoInicio;
    int totalMensajes;
    std::mutex mutexUsuarios;
    std::vector<Usuario> usuarios;
    std::map<int, std::chrono::steady_clock::time_point> tiemposUltimosMensajes;  // Declaración del mapa
};

#endif // SERVIDORCHAT_H
