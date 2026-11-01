# GitHub Actions: проверки и выпуск прошивки

Workflow: [firmware.yml](../.github/workflows/firmware.yml).
Используется один GitHub-hosted runner: Ubuntu 24.04. Собственный сервер CI не нужен.

## Что запускается

| Событие | Работа |
| --- | --- |
| Pull request в дефолтную ветку | Проверка версии/CHANGELOG, сборка ESP-07 и лимита OTA, тесты C++, Python, Chromium и WebKit |
| Push в дефолтную ветку, включая merge PR | Проверка версии, итоговая сборка и лимит OTA, упаковка, публикация Release |
| PR в другую ветку, push в другую ветку | Задания пропускаются |
| Создание тега, расписание, ручной запуск | Не запускают workflow |

В GitHub аналог Merge Request называется **Pull Request** (PR).
Имя основной ветки не зашито: используется github.event.repository.default_branch.
Выражения нельзя подставлять в фильтр on.branches, поэтому ненужные события могут
отображаться в Actions как запуск с пропущенными заданиями.

В релизной сборке тесты не запускаются повторно. Проверка PR использует результат
пробного слияния с целевой веткой; релиз собирается из точного SHA push.
Поэтому защита дефолтной ветки обязательна для выпуска только проверенных изменений.

## Первичная настройка в GitHub

1. Проверьте дефолтную ветку репозитория в **Settings → General → Default branch**.
2. Откройте **Settings → Actions → General**:
   - включите Actions, если они отключены;
   - разрешите actions организации GitHub (actions/checkout, setup-python,
     setup-node, upload-artifact, download-artifact);
   - стандартные **Workflow permissions** можно оставить **Read repository contents
     and packages permissions**: workflow сам запрашивает contents: write только
     для задания публикации. Политика организации должна разрешать это;
   - пункт **Allow GitHub Actions to create and approve pull requests** не нужен.
3. Добавьте изменения CI в отдельную ветку и откройте PR в дефолтную.
   Дождитесь успешной проверки **Firmware checks**. Для PR из fork первый запуск
   может потребовать нажатия **Approve and run**.
4. После появления первой проверки откройте **Settings → Rules → Rulesets →
   New ruleset → New branch ruleset**:
   - имя, например Default branch CI, **Enforcement status: Active**;
   - **Target branches → Add target → Include default branch**;
   - включите **Require a pull request before merging**;
   - включите **Require status checks to pass**, добавьте **Firmware checks**
     с источником GitHub Actions;
   - включите **Require branches to be up to date before merging**;
   - запретите force push и удаление ветки;
   - для личного репозитория не требуйте чужое одобрение, если работаете один.
   Не добавляйте релизные задания как обязательные PR-проверки.
   Если Rulesets недоступны для вашего тарифа/видимости репозитория, используйте
   **Settings → Branches → Add branch protection rule** с теми же требованиями.
   Возможности защиты зависят от тарифа GitHub.
5. Слейте PR. Во вкладке **Actions → Firmware CI** появятся задания
   **Firmware package**, затем **Publish firmware release**.
6. Откройте **Releases** на главной странице репозитория. Будет опубликован
   v<версия>, например v1.2.1 для текущего FirmwareInfo.h.

Секреты, PAT, пароль OTA и ручное создание тегов не нужны.
GitHub автоматически выдаёт краткоживущий GITHUB_TOKEN.
Если в репозитории есть правила для тегов v*, они должны разрешать создание
релизного тега GitHub Actions; workflow не обходит запреты организации/репозитория.
Merge queue этой конфигурацией не обслуживается: для неё отдельно нужны проверки
события merge_group.

## Версия и заметки

Единственный источник версии — Firmware::Version в
[include/FirmwareInfo.h](../include/FirmwareInfo.h), формат X.Y.Z.
Название Release и Git-тег — vX.Y.Z.

В начале [CHANGELOG.md](../CHANGELOG.md) должна быть непустая запись той же версии:

~~~markdown
# [1.2.2] - YYYY-MM-DD

### Изменения

#### Сборка и разработка

  - Описание изменений выпуска.
~~~

В настоящем заголовке замените YYYY-MM-DD датой выпуска.
Заметки Release берутся только из тела этой записи; старые записи не включаются.

**Каждый новый коммит в дефолтной ветке означает новый релиз.**
Поэтому перед каждым слиянием, в том числе документации, повышайте версию
и добавляйте новую запись CHANGELOG. В текущей истории пока нет релизных тегов:
первый запуск сможет опубликовать текущую версию без её автоматического повышения.
После первого выпуска проверка PR отклонит уже использованную версию.
До слияния нескольких PR с одной версией обновите версию в оставшихся PR.

Существующий тег никогда не переносится на другой коммит.
Повторная публикация того же SHA с уже готовым релизом ничего не перезаписывает.
Прерванная публикация оставляет черновик: **Actions → нужный запуск →
Re-run failed jobs** возобновит загрузку файлов и публикацию.
Сборочный артефакт должен ещё храниться (30 дней); после его удаления используйте
**Re-run all jobs**. Повторная попытка другого SHA с прежней версией завершится
ошибкой — выпустите новую версию.

## Что скачивать

Release содержит:

- wifi-led-controller-X.Y.Z-esp07.bin — прошивка для OTA и UART;
- wifi-led-controller-X.Y.Z-esp07.zip — тот же BIN, build-info.json и заметки;
- SHA256SUMS — SHA256 всех остальных файлов релиза;
- build-info.json — версия, SHA коммита, ID запуска, плата, размер и SHA256 BIN;
- release-notes.md — заметки из CHANGELOG.

Для OTA выберите **BIN**, не ZIP. Настройки LittleFS не входят в пакет.
Публикуется стандартная конфигурация исходников, включая заданный в FirmwareInfo.h
пароль OTA; CI не подставляет другой пароль.

На Windows сверить SHA256 можно командой:

~~~powershell
Get-FileHash .\wifi-led-controller-1.2.1-esp07.bin -Algorithm SHA256
~~~

На Linux, скачав все файлы в одну папку:

~~~bash
sha256sum --check SHA256SUMS
~~~

Файлы сохраняются как вложения Release. Раздел **Packages** не используется:
GitHub Packages не имеет Generic Package Registry для произвольных BIN/ZIP.
Промежуточный Actions artifact хранится 30 дней, отчёты браузерных тестов — 14 дней;
этот срок не удаляет вложения опубликованного Release.

## Локальная проверка

~~~powershell
python -m pip install platformio==6.1.18
python scripts/ci_release.py check
python -m platformio run -e esp07
python scripts/test_native.py
python -m unittest discover -s tests -p "test_*.py" -v
npm ci
npx playwright install chromium webkit
npm test
python scripts/ci_release.py package
~~~

Папка dist должна быть пустой перед упаковкой. Команды check и package не обращаются
к GitHub; publish предназначена для CI и использует токен только из окружения.
Тесты публикации имитируют GitHub API и ничего не публикуют.
Проверка размера выполняется штатным post-build скриптом и повторно при упаковке.

## Если запуск завершился ошибкой

- **Firmware checks**: откройте упавший шаг; браузерный JUnit и снимки находятся
  в артефакте test-results. Логи компиляции и остальных тестов — прямо в Actions.
- **Version already exists**: обновите FirmwareInfo.h и CHANGELOG перед слиянием.
- **HTTP 403 / Resource not accessible by integration**: проверьте разрешения
  Actions организации, contents: write и ограничения на релизные теги.
- Ошибка установки PlatformIO/браузеров: проверьте доступность сети и повторите
  запуск; локальный успешный тест не подтверждает доступность сервисов на runner.
- Ошибка загрузки вложения: исправьте причину и повторите упавшее задание;
  черновик не считается опубликованным релизом.

## Документация GitHub

- [Настройки Actions и GITHUB_TOKEN](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/enabling-features-for-your-repository/managing-github-actions-settings-for-a-repository)
- [Правила веток и обязательные проверки](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-rulesets/available-rules-for-rulesets)
- [Форматы GitHub Packages](https://docs.github.com/en/packages/learn-github-packages/introduction-to-github-packages)
- [Управление релизами и черновиками](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository)

