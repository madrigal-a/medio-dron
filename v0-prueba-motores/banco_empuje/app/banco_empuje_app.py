"""
Banco de empuje - programa de PC
Controla el ESP32 (firmware banco_empuje.ino) por USB y guía la prueba:
sube el acelerador por escalones, espera a que se estabilice, tú anotas lo que
marca la báscula (y la corriente de la fuente, si quieres) y al final guarda un CSV
y la gráfica.

Uso:  doble clic en INICIAR.bat   (o:  python banco_empuje_app.py)
Requiere: Python 3.9+, pyserial, matplotlib (INICIAR.bat los instala).
"""
import csv
import datetime as dt
import os
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox

import serial
import serial.tools.list_ports

try:
    from matplotlib.figure import Figure
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
    HAY_MPL = True
except ImportError:
    HAY_MPL = False

BAUD = 115200
LATIDO_S = 0.5
CARPETA_RES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "resultados")


class Banco(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Banco de empuje EX1103")
        self.geometry("1050x680")
        self.minsize(900, 600)

        self.ser = None
        self.rx = queue.Queue()
        self.vivo = False
        self.armado = False
        self.datos = []           # (pct, gramos, amperes)
        self.prueba_activa = False
        self.pasos = []
        self.paso_i = 0
        self.esperando_lectura = False
        self._job_estab = None
        self._job_slider = None

        self._ui()
        self.bind("<space>", lambda e: self.paro())
        self.bind("<Escape>", lambda e: self.paro())
        self.protocol("WM_DELETE_WINDOW", self.cerrar)
        self.after(50, self._leer_cola)
        self.refrescar_puertos()

    # ------------------------------------------------------------- interfaz
    def _ui(self):
        top = ttk.Frame(self, padding=8); top.pack(fill="x")
        ttk.Label(top, text="Puerto:").pack(side="left")
        self.cb_puerto = ttk.Combobox(top, width=28, state="readonly")
        self.cb_puerto.pack(side="left", padx=4)
        ttk.Button(top, text="↻", width=3, command=self.refrescar_puertos).pack(side="left")
        self.btn_con = ttk.Button(top, text="Conectar", command=self.conectar)
        self.btn_con.pack(side="left", padx=6)
        self.lbl_estado = ttk.Label(top, text="Desconectado", foreground="gray")
        self.lbl_estado.pack(side="left", padx=10)

        paro = tk.Button(top, text="PARO  (Espacio)", bg="#c62828", fg="white",
                         font=("Segoe UI", 12, "bold"), activebackground="#8e0000",
                         command=self.paro, padx=16)
        paro.pack(side="right")

        cuerpo = ttk.Frame(self, padding=(8, 0, 8, 8)); cuerpo.pack(fill="both", expand=True)
        izq = ttk.Frame(cuerpo); izq.pack(side="left", fill="y")
        der = ttk.Frame(cuerpo); der.pack(side="left", fill="both", expand=True, padx=(10, 0))

        # --- control manual
        man = ttk.LabelFrame(izq, text="1. Control manual", padding=8); man.pack(fill="x")
        fila = ttk.Frame(man); fila.pack(fill="x")
        self.btn_armar = ttk.Button(fila, text="Armar ESC", command=self.armar, state="disabled")
        self.btn_armar.pack(side="left")
        ttk.Label(fila, text="  Límite %:").pack(side="left")
        self.var_max = tk.IntVar(value=35)
        sp = ttk.Spinbox(fila, from_=5, to=100, increment=5, width=5, textvariable=self.var_max,
                         command=self.cambiar_max)
        sp.pack(side="left")
        sp.bind("<Return>", lambda e: self.cambiar_max())

        self.var_acc = tk.IntVar(value=0)
        self.lbl_acc = ttk.Label(man, text="Acelerador: 0 %", font=("Segoe UI", 11, "bold"))
        self.lbl_acc.pack(anchor="w", pady=(8, 0))
        self.slider = ttk.Scale(man, from_=0, to=35, orient="horizontal", length=300,
                                command=self._mover_slider)
        self.slider.pack(fill="x")
        ttk.Label(man, text="Sube suave. Espacio o Esc = paro inmediato.",
                  foreground="gray").pack(anchor="w")

        # --- prueba guiada
        pr = ttk.LabelFrame(izq, text="2. Prueba por escalones", padding=8); pr.pack(fill="x", pady=8)
        g = ttk.Frame(pr); g.pack(fill="x")
        ttk.Label(g, text="Motor / nombre:").grid(row=0, column=0, sticky="w")
        self.var_nombre = tk.StringVar(value="motor1")
        ttk.Entry(g, textvariable=self.var_nombre, width=14).grid(row=0, column=1, sticky="w")
        ttk.Label(g, text="Escalón %:").grid(row=1, column=0, sticky="w")
        self.var_paso = tk.IntVar(value=5)
        ttk.Spinbox(g, from_=1, to=25, width=5, textvariable=self.var_paso).grid(row=1, column=1, sticky="w")
        ttk.Label(g, text="Estabilizar (s):").grid(row=2, column=0, sticky="w")
        self.var_estab = tk.DoubleVar(value=3.0)
        ttk.Spinbox(g, from_=1, to=15, increment=0.5, width=5,
                    textvariable=self.var_estab).grid(row=2, column=1, sticky="w")

        fb = ttk.Frame(pr); fb.pack(fill="x", pady=6)
        self.btn_iniciar = ttk.Button(fb, text="▶ Iniciar prueba", command=self.iniciar_prueba,
                                      state="disabled")
        self.btn_iniciar.pack(side="left")
        ttk.Button(fb, text="Guardar CSV", command=self.guardar).pack(side="left", padx=6)
        ttk.Button(fb, text="Limpiar", command=self.limpiar).pack(side="left")

        self.lbl_instr = ttk.Label(pr, text="Pon la báscula en TARA con el motor montado.",
                                   wraplength=310, font=("Segoe UI", 10, "bold"), foreground="#1565c0")
        self.lbl_instr.pack(anchor="w", pady=(4, 6))
        le = ttk.Frame(pr); le.pack(fill="x")
        ttk.Label(le, text="Báscula (g):").grid(row=0, column=0, sticky="w")
        self.ent_g = ttk.Entry(le, width=10, font=("Segoe UI", 12)); self.ent_g.grid(row=0, column=1)
        ttk.Label(le, text="Corriente (A, opcional):").grid(row=1, column=0, sticky="w")
        self.ent_a = ttk.Entry(le, width=10, font=("Segoe UI", 12)); self.ent_a.grid(row=1, column=1)
        self.btn_reg = ttk.Button(le, text="Registrar  (Enter)", command=self.registrar, state="disabled")
        self.btn_reg.grid(row=2, column=0, columnspan=2, sticky="we", pady=4)
        self.ent_g.bind("<Return>", lambda e: self.registrar())
        self.ent_a.bind("<Return>", lambda e: self.registrar())

        # --- log
        lg = ttk.LabelFrame(izq, text="Mensajes del ESP32", padding=4); lg.pack(fill="both", expand=True)
        self.txt = tk.Text(lg, height=8, width=44, font=("Consolas", 9), state="disabled")
        self.txt.pack(fill="both", expand=True)

        # --- tabla + gráfica
        cols = ("pct", "g", "a", "gw")
        self.tabla = ttk.Treeview(der, columns=cols, show="headings", height=8)
        for c, t, w in zip(cols, ("Acelerador %", "Empuje (g)", "Corriente (A)", "g/W @7.4V"),
                           (100, 100, 100, 100)):
            self.tabla.heading(c, text=t); self.tabla.column(c, width=w, anchor="center")
        self.tabla.pack(fill="x")

        if HAY_MPL:
            self.fig = Figure(figsize=(6, 4), dpi=100)
            self.ax = self.fig.add_subplot(111)
            self.canvas = FigureCanvasTkAgg(self.fig, master=der)
            self.canvas.get_tk_widget().pack(fill="both", expand=True, pady=(8, 0))
            self._graficar()
        else:
            ttk.Label(der, text="(Instala matplotlib para ver la gráfica)").pack(pady=20)

    # ------------------------------------------------------------- serie
    def refrescar_puertos(self):
        ps = list(serial.tools.list_ports.comports())
        vals = [f"{p.device} — {p.description}" for p in ps]
        self.cb_puerto["values"] = vals
        if vals and not self.cb_puerto.get():
            # prefiere chips típicos del ESP32
            pref = [v for v in vals if any(k in v.upper() for k in ("CP210", "CH340", "CH910", "USB SERIAL", "UART"))]
            self.cb_puerto.set(pref[0] if pref else vals[0])

    def conectar(self):
        if self.ser:
            self.desconectar(); return
        sel = self.cb_puerto.get()
        if not sel:
            messagebox.showwarning("Puerto", "Conecta el ESP32 por USB y presiona ↻"); return
        puerto = sel.split(" — ")[0]
        try:
            self.ser = serial.Serial(puerto, BAUD, timeout=0.1)
        except Exception as e:
            messagebox.showerror("Error", f"No pude abrir {puerto}:\n{e}"); return
        self.vivo = True
        threading.Thread(target=self._hilo_rx, daemon=True).start()
        threading.Thread(target=self._hilo_latido, daemon=True).start()
        self.btn_con.config(text="Desconectar")
        self.btn_armar.config(state="normal")
        self.lbl_estado.config(text=f"Conectado a {puerto} (sin armar)", foreground="#ef6c00")
        self.log(f"Conectado a {puerto}")

    def desconectar(self):
        self.paro()
        self.vivo = False
        time.sleep(0.15)
        try: self.ser.close()
        except Exception: pass
        self.ser = None
        self.armado = False
        self.btn_con.config(text="Conectar")
        for b in (self.btn_armar, self.btn_iniciar, self.btn_reg): b.config(state="disabled")
        self.lbl_estado.config(text="Desconectado", foreground="gray")

    def enviar(self, txt):
        if not self.ser: return
        try: self.ser.write((txt + "\n").encode())
        except Exception as e: self.rx.put(f"[error al enviar: {e}]")

    def _hilo_rx(self):
        buf = b""
        while self.vivo and self.ser:
            try:
                d = self.ser.read(256)
            except Exception as e:
                self.rx.put(f"[puerto perdido: {e}]"); self.rx.put("__PERDIDO__"); break
            if d:
                buf += d
                while b"\n" in buf:
                    l, buf = buf.split(b"\n", 1)
                    self.rx.put(l.decode(errors="replace").strip())

    def _hilo_latido(self):
        while self.vivo and self.ser:
            self.enviar("hb")
            time.sleep(LATIDO_S)

    def _leer_cola(self):
        try:
            while True:
                l = self.rx.get_nowait()
                if l == "__PERDIDO__":
                    self.desconectar(); continue
                if l:
                    self.log(l)
                    if l.startswith("Armado"):
                        self.armado = True
                        self.btn_iniciar.config(state="normal")
                        self.lbl_estado.config(text="ARMADO — cuidado con la hélice", foreground="#c62828")
        except queue.Empty:
            pass
        self.after(50, self._leer_cola)

    def log(self, s):
        self.txt.config(state="normal")
        self.txt.insert("end", s + "\n"); self.txt.see("end")
        self.txt.config(state="disabled")

    # ------------------------------------------------------------- control
    def armar(self):
        self.slider.set(0)
        self.enviar(f"max {self.var_max.get()}")
        self.enviar("armar")
        self.log("Armando… (3 s)")

    def cambiar_max(self):
        try: m = max(5, min(100, int(self.var_max.get())))
        except Exception: return
        self.slider.config(to=m)
        if self.slider.get() > m: self.slider.set(m)
        self.enviar(f"max {m}")

    def _mover_slider(self, v):
        pct = int(float(v))
        self.lbl_acc.config(text=f"Acelerador: {pct} %")
        if self.prueba_activa: return
        if self._job_slider: self.after_cancel(self._job_slider)
        self._job_slider = self.after(120, lambda: self.enviar(str(pct)))

    def paro(self):
        self.enviar("s")
        self.slider.set(0)
        self.lbl_acc.config(text="Acelerador: 0 %")
        if self.prueba_activa:
            self.terminar_prueba("Prueba detenida con PARO.")

    # ------------------------------------------------------------- prueba
    def iniciar_prueba(self):
        if not self.armado:
            messagebox.showinfo("Armar", "Primero arma el ESC."); return
        if not messagebox.askokcancel("Prueba",
                                      "¿Báscula en TARA, base pegada, lentes puestos y nadie cerca de la hélice?"):
            return
        paso = max(1, int(self.var_paso.get())); m = int(self.var_max.get())
        self.pasos = list(range(paso, m + 1, paso))
        self.paso_i = 0
        self.prueba_activa = True
        self.btn_iniciar.config(state="disabled")
        self._siguiente_paso()

    def _siguiente_paso(self):
        if not self.prueba_activa: return
        if self.paso_i >= len(self.pasos):
            self.terminar_prueba("¡Prueba completa! Guarda el CSV."); return
        pct = self.pasos[self.paso_i]
        self.enviar(str(pct)); self.slider.set(pct)
        self.esperando_lectura = False
        self.btn_reg.config(state="disabled")
        seg = float(self.var_estab.get())
        self.lbl_instr.config(text=f"Escalón {self.paso_i + 1}/{len(self.pasos)}: {pct} %  — estabilizando {seg:.1f} s…",
                              foreground="#ef6c00")
        self._job_estab = self.after(int(seg * 1000), self._pedir_lectura)

    def _pedir_lectura(self):
        pct = self.pasos[self.paso_i]
        self.esperando_lectura = True
        self.btn_reg.config(state="normal")
        self.lbl_instr.config(text=f"{pct} %: escribe lo que marca la báscula y presiona Enter",
                              foreground="#2e7d32")
        self.bell()
        self.ent_g.delete(0, "end"); self.ent_a.delete(0, "end")
        self.ent_g.focus_set()

    def registrar(self):
        if not self.esperando_lectura: return
        try:
            g = float(self.ent_g.get().replace(",", "."))
        except ValueError:
            self.ent_g.focus_set(); return
        a_txt = self.ent_a.get().strip().replace(",", ".")
        a = float(a_txt) if a_txt else None
        pct = self.pasos[self.paso_i]
        self.datos.append((pct, g, a))
        gw = f"{g / (a * 7.4):.2f}" if a else ""
        self.tabla.insert("", "end", values=(pct, f"{g:.1f}", f"{a:.2f}" if a else "", gw))
        self._graficar()
        self.paso_i += 1
        self._siguiente_paso()

    def terminar_prueba(self, msg):
        self.prueba_activa = False
        self.esperando_lectura = False
        if self._job_estab: self.after_cancel(self._job_estab); self._job_estab = None
        self.enviar("0"); self.slider.set(0)
        self.btn_reg.config(state="disabled")
        if self.armado: self.btn_iniciar.config(state="normal")
        self.lbl_instr.config(text=msg, foreground="#1565c0")

    # ------------------------------------------------------------- datos
    def _graficar(self):
        if not HAY_MPL: return
        self.ax.clear()
        if self.datos:
            x = [d[0] for d in self.datos]; y = [d[1] for d in self.datos]
            self.ax.plot(x, y, "o-", color="#1565c0", label="Empuje")
            for xi, yi in zip(x, y):
                self.ax.annotate(f"{yi:.0f}", (xi, yi), textcoords="offset points", xytext=(0, 6),
                                 ha="center", fontsize=8)
        self.ax.set_xlabel("Acelerador (%)"); self.ax.set_ylabel("Empuje (g)")
        self.ax.set_title(f"Curva de empuje — {self.var_nombre.get()}")
        self.ax.grid(alpha=0.3)
        self.fig.tight_layout(); self.canvas.draw_idle()

    def guardar(self):
        if not self.datos:
            messagebox.showinfo("Guardar", "Todavía no hay datos."); return
        os.makedirs(CARPETA_RES, exist_ok=True)
        base = f"{self.var_nombre.get().strip() or 'motor'}_{dt.datetime.now():%Y%m%d_%H%M%S}"
        ruta = os.path.join(CARPETA_RES, base + ".csv")
        with open(ruta, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["acelerador_pct", "empuje_g", "corriente_A", "voltaje_V", "g_por_W"])
            for pct, g, a in self.datos:
                w.writerow([pct, g, a if a is not None else "", 7.4,
                            round(g / (a * 7.4), 3) if a else ""])
        if HAY_MPL: self.fig.savefig(os.path.join(CARPETA_RES, base + ".png"), dpi=150)
        messagebox.showinfo("Guardado", f"Guardado en:\n{os.path.abspath(ruta)}")

    def limpiar(self):
        if self.datos and not messagebox.askyesno("Limpiar", "¿Borrar los datos de la tabla?"): return
        self.datos.clear()
        for i in self.tabla.get_children(): self.tabla.delete(i)
        self._graficar()

    def cerrar(self):
        if self.ser: self.desconectar()
        self.destroy()


if __name__ == "__main__":
    Banco().mainloop()
