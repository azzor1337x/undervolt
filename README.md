
---

# \:rocket: C60 Undervolt & Tweak Guide

Este guia mostra como aplicar tweaks de desempenho e energia no processador AMD C-60 utilizando Alpine Linux.

---

## \:information\_source: Sobre

Este tweak aplica undervolt na CPU e configura o *governador de frequência* para `ondemand`, buscando melhorar a eficiência energética e o desempenho geral em sistemas com hardware limitado, como netbooks ou thin clients.

⚠️ **Aviso:** Aplicações incorretas de undervolt podem causar instabilidades no sistema. Use por sua conta e risco.

Requisitos:

* Alpine Linux
* Privilégios de root
* Conexão com a internet

---

## \:package: Instalar dependências

```sh
apk add build-base linux-headers util-linux cpufrequtils git
```

---

## \:gear: Criar script para configurar o governador de CPU

Crie o script que será executado no boot:

```sh
nano /etc/local.d/cpufreq.start
```

Conteúdo do script:

```sh
#!/bin/sh
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo ondemand > "$cpu"
done
```

Dê permissão de execução:

```sh
chmod +x /etc/local.d/cpufreq.start
```

---

## \:floppy\_disk: Clonar o repositório

```sh
cd /
git clone https://github.com/azzor1337x/undervolt.git
```

---

## \:hammer: Compilar e instalar o programa

```sh
cd undervolt
make
```

```sh
cp undervolt /usr/bin/
chmod +x /usr/bin/undervolt
```

```sh
rm -rf /undervolt
```

---

## \:rocket: Aplicar automaticamente no boot

Crie o script de inicialização:

```sh
nano /etc/local.d/c60-tweak.start
```

Conteúdo do script:

```sh
#!/bin/sh
modprobe msr
sleep 1
/usr/bin/undervolt -p 0:0x28 -p 1:0x28,3 -p 2:0x38
```

Dê permissão de execução:

```sh
chmod +x /etc/local.d/c60-tweak.start
```

Ative o serviço `local` para garantir que o script seja executado no boot:

```sh
rc-update add local default
```

---

## \:white\_check\_mark: Finalizando

Reinicie o sistema:

```sh
reboot
```

Após reiniciar, verifique se os ajustes estão ativos:

```sh
cpufreq-info
```

```sh
watch -n1 "cat /proc/cpuinfo | grep MHz"
```

```sh
undervolt -r
```

Se os P-states estiverem corretos (`0x28`, `3.00`, `0x38`, etc.), o tweak está ativo com sucesso.

---
