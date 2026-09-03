const select = document.querySelector("#build-select");
const status = document.querySelector("#catalog-status");
const details = document.querySelector("#build-details");
const kind = document.querySelector("#build-kind");
const version = document.querySelector("#build-version");
const sourceRow = document.querySelector("#build-source-row");
const source = document.querySelector("#build-source");
const installer = document.querySelector("#installer");
const activateButton = installer.querySelector('[slot="activate"]');

let builds = [];

function describeKind(build) {
  if (build.kind === "candidate") {
    if (build.source === "main") {
      return "Development build from main";
    }
    return build.pr ? `Pull request #${build.pr}` : "CI candidate";
  }
  if (build.draft) {
    return build.prerelease ? "Draft prerelease" : "Draft release";
  }
  return build.prerelease ? "Published prerelease" : "Published release";
}

function selectBuild(build) {
  installer.manifest = new URL(build.manifest, window.location.href).href;
  activateButton.disabled = false;
  kind.textContent = describeKind(build);
  version.textContent = build.version;

  if (build.run_url) {
    source.href = build.run_url;
    sourceRow.hidden = false;
  } else {
    source.removeAttribute("href");
    sourceRow.hidden = true;
  }

  details.hidden = false;
}

function populateCatalog(catalog) {
  if (!catalog || !Array.isArray(catalog.builds)) {
    throw new Error("catalog.json does not contain a builds array");
  }

  builds = catalog.builds;
  select.replaceChildren();

  if (builds.length === 0) {
    const option = document.createElement("option");
    option.textContent = "No flashable builds are available";
    select.append(option);
    status.textContent = "No builds";
    return;
  }

  builds.forEach((build, index) => {
    const option = document.createElement("option");
    option.value = String(index);
    option.textContent = `${build.label} — ${build.version}`;
    select.append(option);
  });

  select.disabled = false;
  status.textContent = `${builds.length} build${builds.length === 1 ? "" : "s"} available`;
  selectBuild(builds[0]);
}

select.addEventListener("change", () => {
  const build = builds[Number.parseInt(select.value, 10)];
  if (build) {
    selectBuild(build);
  }
});

try {
  const response = await fetch("catalog.json", { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`catalog request failed with HTTP ${response.status}`);
  }
  populateCatalog(await response.json());
} catch (error) {
  console.error(error);
  status.textContent = "Catalog unavailable";
  select.replaceChildren();
  const option = document.createElement("option");
  option.textContent = "Unable to load firmware catalog";
  select.append(option);
}
