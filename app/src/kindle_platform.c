#include "kindle_platform.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Mantém o Kindle acordado só enquanto este app está na tela — mesmo padrão (e mesma
 * justificativa) do pet_dashboard no projeto irmão `kindle`, ver
 * ../../kindle/docs/findings/screensaver-app-lifecycle.md: liga no início, desliga em todo
 * caminho de saída alcançável (destroy da janela, SIGTERM do kill de deploy, SIGINT).
 * SIGKILL é o único caminho que isso não pega — a propriedade ficaria travada em 1 até
 * reboot ou reversão manual. */
void kindle_platform_keep_awake(bool awake) {
    int value = awake ? 1 : 0;
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "lipc-set-prop -i com.lab126.powerd preventScreenSaver %d", value);
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "kindow: lipc-set-prop preventScreenSaver=%d falhou (rc=%d)\n",
                value, rc);
    }
}

/* O window manager do Kindle (Awesome WM) só mapeia/exibe em tela cheia janelas cujo título
 * segue esse esquema key-value (L:layer, N:role, ID:reverse-domain, PC:N esconde a barra de
 * status do Kindle) — descoberto no projeto irmão `kindle`, replicado aqui do mesmo jeito
 * que o pet_dashboard já faz. Sem isso a janela fica como stub 10x10 nunca mapeado, mesmo
 * com o processo rodando normalmente. */
const char *kindle_platform_window_title(void) {
    return "L:A_N:application_ID:com.eduardo.kindowclient_PC:N";
}

const char *kindle_platform_data_dir(void) {
    static const char *path = "/mnt/us/kindow";
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "kindow: não consegui criar %s (errno=%d)\n", path, errno);
    }
    return path;
}

bool kindle_platform_language_is_portuguese(void) {
    const char *override = getenv("KINDOW_LANG");
    if (override) {
        return override[0] == 'p' || override[0] == 'P';
    }

    /* O firmware guarda o idioma escolhido nas configurações como linhas
     * "LANG=pt_BR.utf8" / "LC_ALL=..." neste arquivo (verificado no device real).
     * Só o prefixo da língua importa aqui. */
    FILE *f = fopen("/var/local/system/locale", "r");
    if (!f) {
        return false; /* sem arquivo (ex. rodando fora do Kindle): inglês */
    }
    char line[64];
    bool portuguese = false;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "LANG=pt", 7) == 0) {
            portuguese = true;
            break;
        }
    }
    fclose(f);
    return portuguese;
}
