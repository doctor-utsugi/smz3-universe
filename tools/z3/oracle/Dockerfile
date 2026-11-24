# PHP oracle for the A Link to the Past port (core/z3gen/): alttp_php_randomizer at the pinned commit, its composer.lock
# dependencies, and z3oracle.php next to it. Build and use with tools/z3/oracle/oracle.sh.
FROM php:8.1.29-cli-bookworm

ARG ALTTPR_COMMIT=219fcafd029dab597b8db400efafd8f56f8b4edb
COPY --from=composer:2.7.7 /usr/bin/composer /usr/bin/composer

# libunwind8: the Enemizer (vendor/z3/enemizer_linux, a self-contained .NET Core build) needs it to start.
RUN apt-get update && apt-get install -y --no-install-recommends git unzip libzip-dev libicu-dev libgmp-dev libunwind8 \
    && docker-php-ext-install bcmath zip intl pcntl gmp \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /alttpr
RUN git init -q . && git remote add origin https://github.com/sporchia/alttp_php_randomizer.git \
    && git fetch -q --depth 1 origin "$ALTTPR_COMMIT" && git checkout -q FETCH_HEAD && rm -rf .git

# composer.lock pins every package, z3/randomizer at dcb0a2b4 included. No scripts: package discovery needs nothing here.
RUN composer install --no-dev --no-scripts --no-interaction --prefer-dist --no-progress \
    --ignore-platform-req=ext-pcntl --ignore-platform-req=ext-posix

# Seedable RNG, oracle only: get_random_int() draws from mt_rand once z3oracle.php's oracle_seed() has run.
RUN sed -i 's|    return random_int(\$min, \$max);|    return empty($GLOBALS["Z3_ORACLE_SEEDED"]) ? random_int($min, $max) : mt_rand($min, $max);|' app/Helpers/number.php \
    && grep -q Z3_ORACLE_SEEDED app/Helpers/number.php

RUN cp .env.example .env && sed -i -e 's/^CACHE_DRIVER=.*/CACHE_DRIVER=array/' -e 's/^APP_KEY=.*/APP_KEY=base64:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=/' .env \
    && printf 'LOG_CHANNEL=stderr\nLOG_LEVEL=error\n' >> .env

RUN chmod -R a+rwX storage bootstrap/cache
# The Enemizer's .NET Core 2.0 finds no ICU it knows on bookworm (and reads no environment switch for it); it only
# parses numbers, so invariant culture is enough.
RUN sed -i 's|"configProperties": {|"configProperties": {\n      "System.Globalization.Invariant": true,|' \
        vendor/z3/enemizer_linux/EnemizerCLI.Core.runtimeconfig.json \
    && grep -q Globalization.Invariant vendor/z3/enemizer_linux/EnemizerCLI.Core.runtimeconfig.json
COPY z3oracle.php /alttpr/z3oracle.php
WORKDIR /work
ENTRYPOINT ["php", "/alttpr/z3oracle.php"]
