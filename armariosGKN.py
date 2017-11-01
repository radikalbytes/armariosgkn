"""
Copyright 2017 Alfredo Prado Vega
@radikalbytes
http://www.radikalbytes.com
This work is licensed under the Creative Commons Attribution-ShareAlike 3.0
Unported License. To view a copy of this license, visit
http://creativecommons.org/licenses/by-sa/3.0/ or send a letter to
Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
"""
import sys
import time
import matplotlib.pyplot as plt
import matplotlib as mpl
import pandas as pd
import requests
import http.client
import matplotlib.backends.backend_tkagg # evita error al compilar
import matplotlib.ticker as ticker
import matplotlib.dates as mdates
#import msvcrt  #para windows
http.client._MAXHEADERS = 10000 # evitar fallo por max headers html
tick_spacing = 0.08
dateparse = lambda x: pd.datetime.strptime(x, '%d/%m/%y %H:%M:%S')
def main():
    if len(sys.argv) >= 3:
        ip_addres = (sys.argv[1]) # Introducir ip por argumento
        num_maquina = sys.argv[2] # Introducir numero de maquina por argumento
    else:
        print ("Necesita pasar como parametro la IP de la maquina a monitorizar")
        print ("y el nombre de la maquina")
        print ("Ejemplo: py armario.py 192.168.0.15 WQ2533")
        print ("         armario.exe 192.168.0.15 WQ2533")
        sys.exit()
    while True:
        # Peticion GET html al servidor Arduino
        r = requests.get("http://"+ip_addres, verify=False)
        if not r.ok:
            raise RuntimeError(r.text)
        # Pasar los datos recibidos a un fichero csv
        f = open ('data.csv','w')
        f.write(r.text)
        f.close()
        # Parsear csv y normalizar campo fecha
        data=pd.read_csv('data.csv', parse_dates=['timestamp'], date_parser=dateparse)
        # Indexar por numero de muestra (orden)
        data.set_index("timestamp", inplace=True)
        # Tamaño ventana de graficas
        fig, ax = plt.subplots(3, sharex=True, figsize=(7.5,3))
        titulo = num_maquina + "   " + ip_addres
        xfmt = mdates.DateFormatter('%d/%m/%y %H:%M')


        for j in range(3):
            ax[j].set_axisbelow(True)
            # Quitamos los bordes superior y derecho
            ax[j].spines["top"].set_visible(False)
            ax[j].spines["right"].set_visible(False)
            # Dejamos sólo los ticks abajo y a la izquierda
            ax[j].get_xaxis().tick_bottom()
            ax[j].get_yaxis().tick_left()
            # Tamano de los labels de los ticks
            ax[j].tick_params(axis='x', width=2, labelsize = 6)
            # Numero de marcas eje x
            ax[j].xaxis.set_major_locator(ticker.LinearLocator(20))
            #ax[j].xaxis.set_minor_locator(ticker.LinearLocator(40))
            #ax[j].yaxis.set_major_locator(ticker.LinearLocator(5))
            #ax[j].yaxis.set_minor_locator(ticker.LinearLocator(9))
            # Formato de los labels D/M/Y H:M
            ax[j].xaxis.set_major_formatter(xfmt)
            ax[j].tick_params(axis='y', width=2, labelsize=6)
        # Rotar los labels 45 grados
        for label in ax[2].xaxis.get_ticklabels():
            label.set_rotation(15)
        for label in ax[0].xaxis.get_ticklabels():
            label.set_visible(False)
        for label in ax[1].xaxis.get_ticklabels():
            label.set_visible(False)

        # Grafica de temperatura
        ax[0].set_xlabel('')
        ax[0].set_ylabel('Temperatura',fontsize = 8)
        ax[0].grid()
        ax[0].set_autoscale_on('True')
        # Grafica de humedad
        ax[1].set_xlabel('')
        ax[1].set_ylabel('Humedad',fontsize = 8)
        ax[1].grid()
        ax[1].set_autoscale_on('True')
        # Grafica de punto de rocio
        ax[2].set_xlabel('Timestamp',fontsize = 10)
        ax[2].set_ylabel('Punto de rocio',fontsize = 8)
        ax[2].grid()
        ax[2].set_autoscale_on('True')
        # plotear graficas
        ax[0].set_title(titulo, fontsize = 12, fontweight = 4, horizontalalignment = 'center')
        line1, = ax[0].plot(data['temperatura'], linewidth=2, color='#CF2017')
        line2, = ax[1].plot(data['humedad'], linewidth=2, color='#17BECF')
        line3, = ax[2].plot(data['pdr'], linewidth=2, color = '#cfb617')
        # plt.pause muestra las graficas actualizadas
        plt.pause(0.001)
        while(1):
          # Peticion GET http de datos al servidor Arduino
          r = requests.get("http://"+ip_addres, verify=False)
          if not r.ok:
              raise RuntimeError(r.text)
          f = open ('data.csv','w')
          f.write(r.text)
          f.close()
          print(".")
          #Codigo de ESCkey para windows
          #if msvcrt.kbhit() and msvcrt.getch()[0] == 27:
          #    sys.exit()
          data=pd.read_csv('data.csv', parse_dates=['timestamp'], date_parser=dateparse)
          # Indexar por numero de muestra (orden)
          data.set_index("timestamp", inplace=True)
          # Actualiza graficas
          line1.set_ydata(data['temperatura'])
          line2.set_ydata(data['humedad'])
          line3.set_ydata(data['pdr'])
          # 10 segundos entre refrescos
          plt.pause(10)

if __name__ == '__main__':
    sys.exit(main())
