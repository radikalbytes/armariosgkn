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
import numpy as np
import numpy.core._methods
import numpy.lib.format
#import msvcrt  #para windows
http.client._MAXHEADERS = 10000 # evitar fallo por max headers html
tick_spacing = 0.08
dateparse = lambda x: pd.datetime.strptime(x, '%d/%m/%y %H:%M:%S')
def main():
    if len(sys.argv) >= 2:
        ip_addres = (sys.argv[1]) # Introducir ip por argumento
    else:
        print ("Necesita pasar como parametro la IP de la maquina a monitorizar")
        print ("Ejemplo: py armario.py 192.168.0.15 ")
        print ("         armario.exe 192.168.0.15")
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
        data1=pd.read_csv('data.csv', parse_dates=['timestamp'], date_parser=dateparse)
        data = data1.head(len(data1['muestra'])-1)
        linea=data1.ix[len(data1)-1]
        print(linea)
        # Indexar por numero de muestra (orden)
        data.set_index("muestra")
        # Tamaño ventana de graficas
        fig, ax = plt.subplots(3, sharex=True, figsize=(7.5,3))
        titulo = "WQ"+ str(linea['maquina']) + "   " + ip_addres + "  " + str(linea['temperatura'])+"ºC  "+str(linea['humedad'])+"%"
        xfmt = mdates.DateFormatter('%d/%m/%y %H:%M')
        dates = data['timestamp']

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
            ax[2].set_xticklabels(dates)
            # Formato de los labels D/M/Y H:M
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
        ax[2].set_xlabel('Timestamp',fontsize = 8)
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
          data1=pd.read_csv('data.csv') #, parse_dates=['timestamp'], date_parser=dateparse)
          data = data1.head(len(data1['muestra'])-1)
          print(len(data1['muestra'])-1)
          linea=data1.ix[len(data1)-1]
          titulo = "WQ"+ str(linea['maquina']) + "   " + ip_addres + "  " + str(linea['temperatura'])+"ºC  "+str(linea['humedad'])+"%"

          # Indexar por numero de muestra (orden)
          data.set_index("muestra")
          dates = data['timestamp']
          datos_ticks = dates.tolist()
          longitud = len(datos_ticks)
          if (longitud<21):
              intervalo = 1
          else:
              intervalo = longitud/18
          datos_ticks_mostrar = datos_ticks[1:int(longitud-1):int(intervalo)]
          print(datos_ticks_mostrar)
          for j in range(3):
              ax[j].relim()
              ax[j].autoscale_view(True,True,True)
          # Actualiza graficas
          line1.set_ydata(data['temperatura'])
          line2.set_ydata(data['humedad'])
          line3.set_ydata(data['pdr'])
          ax[2].set_xticklabels(datos_ticks_mostrar)
          ax[0].set_title(titulo, fontsize = 12, fontweight = 4, horizontalalignment = 'center')

          # 10 segundos entre refrescos
          plt.pause(2)
          fig.canvas.draw()

if __name__ == '__main__':
    sys.exit(main())
