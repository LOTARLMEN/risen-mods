use std::collections::{BTreeMap, HashMap};
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use clap::{Parser, Subcommand};
use risen_formats::archive::StrRef;
use risen_formats::bin_io::ParseError;
use risen_formats::lrent::{DynamicEntity, Lrent};
use risen_formats::pak::Pak;
use risen_formats::props::{PropertyObject, format_value};
use risen_formats::strs::Unknown;
use risen_formats::tab::Tab;
use risen_formats::tple::Tple;
use risen_formats::vfs::{GameVfs, Source, VfsError};
use risen_formats::world::ProjectFile;
use serde::Serialize;
use risen_formats::wrldatasc::WorldData;

mod project;
mod geometry;

const PROJECTS: &str = "data/common/projects";
const TEMPLATES: &str = "data/common/templates";
const STRINGS: &str = "data/compiled/strings";

fn relative_asset(path: &str, scope: &str) -> String {
    path[scope.len() + 1..].replace('/', "\\")
}

fn same_file(a: &Path, b: &Path) -> bool {
    let a = std::fs::canonicalize(a).unwrap_or_else(|_| a.to_path_buf());
    let b = std::fs::canonicalize(b).unwrap_or_else(|_| b.to_path_buf());
    a.to_string_lossy().eq_ignore_ascii_case(&b.to_string_lossy())
}

#[derive(Parser)]
#[command(name = "risen", version, about = "Risen data files: paks, world layers, templates")]
struct Cli {
    /// Game folder (default: RISEN_DIR, else the first parent of the current dir that has data\common)
    #[arg(long, global = true)]
    game: Option<PathBuf>,
    #[command(subcommand)]
    cmd: Cmd,
}

#[derive(Subcommand)]
enum Cmd {
    /// List mounted assets and their effective source (mountlist, archives, loose overrides)
    Vfs {
        filter: Option<String>,
        #[arg(long)]
        json: bool,
    },
    /// List the files of a .pak: stored size, encryption+compression, path
    PakList {
        pak: PathBuf,
        /// Only paths containing this text (case-insensitive)
        filter: Option<String>,
    },
    /// Unpack one file (by name or path inside the pak) to OUT
    PakExtract { pak: PathBuf, name: String, out: PathBuf },
    /// List mounted world layers (.lrent), including loose files and archive patches
    Layers {
        /// Only paths containing this text (case-insensitive)
        filter: Option<String>,
    },
    /// List mounted worlds/sectors: world-data filename or the sector's layer filenames
    Projects {
        /// Only paths containing this text (case-insensitive)
        filter: Option<String>,
        #[arg(long)]
        json: bool,
    },
    /// Read + write every .sec and .wrl of projects.pak and compare bytes (format check)
    ProjectsCheck {
        /// Default: data\common\projects.pak
        pak: Option<PathBuf>,
    },
    /// Show a world's sector list (world name, .wrl, or a .wrldatasc file)
    WorldSectors {
        #[arg(default_value = "World")]
        world: String,
        #[arg(long)]
        json: bool,
    },
    /// Read + write every .wrldatasc of projects.pak and compare bytes
    WorldDataCheck { pak: Option<PathBuf> },
    /// Create an empty layer + sector and register them in the world, with backup and rollback
    LayerCreate {
        name: String,
        #[arg(long, default_value = "World")]
        world: String,
        /// Installed layer + sector whose header versions are used
        #[arg(long, default_value = "Game_Start_Item")]
        like: String,
        /// Optional first entity, built from its template in the new layer
        #[arg(long, requires = "to")]
        spawn: Option<String>,
        #[arg(long, num_args = 3, allow_hyphen_values = true, requires = "spawn")]
        to: Option<Vec<f32>>,
        /// Write a game-shaped staging tree here instead of installing into the game
        #[arg(long)]
        out_dir: Option<PathBuf>,
        #[arg(long)]
        dry_run: bool,
    },
    /// Restore a layer-create backup (game must be closed)
    WorldRestore {
        backup: PathBuf,
        /// Discard subsequent edits to the affected files
        #[arg(long)]
        force: bool,
    },
    /// List the entities of a layer: name, position, property set classes
    Entities {
        /// Layer: a mounted name / path (Harbour_Town_NPC_01) or a .lrent file
        layer: String,
        /// Print every property of every property set
        #[arg(long)]
        props: bool,
        /// Print matrices and bounding volumes
        #[arg(long)]
        geometry: bool,
        #[arg(long)]
        json: bool,
    },
    /// Inspect render models, actor nodes and collision shapes; export resource triangles as OBJ
    Geometry {
        /// Template name/GUID/.tple, a ._xmsh/._xmac/._xcom resource, or a layer with ENTITY
        source: String,
        entity: Option<String>,
        #[arg(long)]
        json: bool,
        /// Export OBJ: render triangles for entities; collision triangles for ._xcom; actors in bind pose
        #[arg(long)]
        obj: Option<PathBuf>,
    },
    /// Unlock an existing rigid body for gravity, or freeze its motion (selected entity only)
    Physics {
        layer: String,
        entity: String,
        #[arg(long, value_parser = ["dynamic", "frozen"])]
        mode: String,
        #[arg(long)]
        out: Option<PathBuf>,
        #[arg(long)]
        dry_run: bool,
    },
    /// Set an existing collider's ledge-climbing permission (selected entity only)
    Climbable {
        layer: String,
        entity: String,
        #[arg(long, action = clap::ArgAction::Set, required = true)]
        enabled: bool,
        #[arg(long)]
        out: Option<PathBuf>,
        #[arg(long)]
        dry_run: bool,
    },
    /// Place an entity/subtree on visual static-mesh surfaces, or on an explicit horizontal plane
    Ground {
        layer: String,
        entity: String,
        /// Use solid colliders for the moving object and static supports (default: visual meshes)
        #[arg(long)]
        collision: bool,
        /// Explicit plane height (cm); otherwise sample visible static mesh triangles
        #[arg(long, allow_negative_numbers = true)]
        height: Option<f32>,
        /// Fit local +Y to the sampled surface, preserving horizontal heading and scale
        #[arg(long)]
        align: bool,
        /// Dense underside grid per axis (3..17); --align defaults to 7, plus mesh vertices
        #[arg(long, value_parser = clap::value_parser!(u16).range(3..=17))]
        samples: Option<u16>,
        /// Refuse a steeper fitted surface when --align is used (degrees)
        #[arg(long, default_value_t = 45.0)]
        max_slope: f32,
        /// Restrict automatic surface search to this layer (default: active world layers)
        #[arg(long, conflicts_with = "height")]
        surface_layer: Option<String>,
        /// Initial world sector configuration used for automatic search
        #[arg(long, default_value = "World", conflicts_with = "surface_layer")]
        world: String,
        #[arg(long, default_value_t = 1000.0)]
        max_drop: f32,
        #[arg(long, default_value_t = 100.0)]
        max_rise: f32,
        /// Positive = clearance; negative = embed into the surface (cm)
        #[arg(long, default_value_t = 0.0, allow_negative_numbers = true)]
        gap: f32,
        #[arg(long)]
        out: Option<PathBuf>,
        #[arg(long)]
        dry_run: bool,
    },
    /// Align world bounding-box faces; negative --gap deliberately overlaps the objects
    Snap {
        layer: String,
        entity: String,
        #[arg(long)]
        against: String,
        #[arg(long)]
        target_layer: Option<String>,
        #[arg(long, default_value = "top", value_parser = ["top", "bottom", "left", "right", "front", "back"])]
        side: String,
        #[arg(long, default_value_t = 0.0, allow_negative_numbers = true)]
        gap: f32,
        /// Also center on the other two axes
        #[arg(long)]
        center: bool,
        #[arg(long)]
        out: Option<PathBuf>,
        #[arg(long)]
        dry_run: bool,
    },
    /// Find entities by name (substring, case-insensitive) or GUID in every mounted world layer
    Find {
        text: String,
        /// Print every property of every property set
        #[arg(long)]
        props: bool,
        #[arg(long)]
        json: bool,
    },
    /// Move an entity: writes the layer as a loose file data\common\projects\<layer path>
    /// (loaded instead of the pak copy; an existing loose file is edited further)
    Move {
        /// Layer name / path inside projects.pak (Game_Start_Item), or a .lrent file
        layer: String,
        /// Entity GUID or index in the layer (see `risen entities`)
        entity: String,
        /// Target world position x y z
        #[arg(long, num_args = 3, allow_negative_numbers = true, conflicts_with = "by", required_unless_present = "by")]
        to: Option<Vec<f32>>,
        /// Offset dx dy dz (y = up)
        #[arg(long, num_args = 3, allow_negative_numbers = true)]
        by: Option<Vec<f32>>,
        /// Write here instead of the loose game file
        #[arg(long)]
        out: Option<PathBuf>,
        /// Only print what would change
        #[arg(long)]
        dry_run: bool,
    },
    /// Copy an entity (with its children), new GUIDs, and place the copy: inside its layer, or
    /// into another layer with --into. Writes the (target) layer like `move`
    Copy {
        /// Layer name / path inside projects.pak (Wild_Outdoor_Monster_01), or a .lrent file
        layer: String,
        /// Entity GUID or index in the layer (see `risen entities`)
        entity: String,
        /// Put the copy into this layer (name / path / .lrent file); without --to/--by it keeps the
        /// original's position
        #[arg(long)]
        into: Option<String>,
        /// World position of the copy x y z
        #[arg(long, num_args = 3, allow_negative_numbers = true, conflicts_with = "by", required_unless_present_any = ["by", "into"])]
        to: Option<Vec<f32>>,
        /// Offset of the copy from the original dx dy dz (y = up)
        #[arg(long, num_args = 3, allow_negative_numbers = true)]
        by: Option<Vec<f32>>,
        /// Number of copies (each further copy is offset by --by again, or stacked at --to)
        #[arg(long, default_value_t = 1)]
        count: usize,
        /// Write here instead of the loose game file
        #[arg(long)]
        out: Option<PathBuf>,
        /// Only print what would change
        #[arg(long)]
        dry_run: bool,
    },
    /// Spawn an entity of a template at a position: copies an existing world instance of the
    /// template and moves the copy there; a template with no instance in the world (or with
    /// --from-template) is built from the template itself (needs --layer)
    Spawn {
        /// Template name (It_Plant_Mushroom, Wolf), GUID, or a .tple file (built directly)
        template: String,
        /// World position x y z (y = up)
        #[arg(long, num_args = 3, allow_negative_numbers = true, required = true)]
        to: Vec<f32>,
        /// Absolute world heading in degrees: 0 = +Z, 90 = +X; keeps pitch, roll and scale
        #[arg(long, allow_negative_numbers = true)]
        yaw: Option<f32>,
        /// Put the copies into this layer (default: the layer with the most instances). The source
        /// instance comes from this layer when it has one, else from the layer with the most
        #[arg(long)]
        layer: Option<String>,
        /// Number of copies, spread on a circle of --spread radius around --to
        #[arg(long, default_value_t = 1)]
        count: usize,
        #[arg(long, default_value_t = 150.0)]
        spread: f32,
        /// Build the entity from the template even when the world has instances to copy (an
        /// instance may carry level-designer changes: inventory, routines, ...)
        #[arg(long)]
        from_template: bool,
        /// Write here instead of the loose game file
        #[arg(long)]
        out: Option<PathBuf>,
        /// Only print what would change
        #[arg(long)]
        dry_run: bool,
    },
    /// Delete an entity with its children from its layer. Warns about other layers that still
    /// reference its GUID (anchors, routines, ...). Writes the layer like `move`
    Delete {
        /// Layer name / path inside projects.pak, or a .lrent file
        layer: String,
        /// Entity GUID or index in the layer (see `risen entities`)
        entity: String,
        /// Write here instead of the loose game file
        #[arg(long)]
        out: Option<PathBuf>,
        /// Only print what would change
        #[arg(long)]
        dry_run: bool,
    },
    /// Read + write every .lrent of a pak and compare bytes (format check)
    LrentCheck {
        /// Default: data\common\projects.pak
        pak: Option<PathBuf>,
    },
    /// List mounted templates: name, GUID, property set classes, path (including loose templates)
    Templates {
        /// Only names / paths containing this text (case-insensitive), or a template GUID
        filter: Option<String>,
        /// Print every property of every property set
        #[arg(long)]
        props: bool,
        #[arg(long)]
        json: bool,
    },
    /// Copy a template under a new internal name with new GUIDs (single templates only).
    /// Adds a loose .tple alongside the source's path; existing files are never overwritten.
    TemplateCopy {
        /// Source template name, GUID, or a .tple file
        template: String,
        /// New internal name (ASCII letters, digits, underscores); no .tple suffix
        name: String,
        /// Write here instead of data/common/templates/<source folder>/<name>.tple.
        /// A file source defaults to its own folder.
        #[arg(long)]
        out: Option<PathBuf>,
        /// Only print what would change
        #[arg(long)]
        dry_run: bool,
    },
    /// Read + write every .tple of a pak and compare bytes (format check)
    TpleCheck {
        /// Default: data\common\templates.pak
        pak: Option<PathBuf>,
    },
    /// Copy every top-level entity of every layer into another layer (in memory), read it back and
    /// compare with the original (bytes with string indices resolved); lists what can't be moved
    CopyCheck {
        /// Target layer (default Game_Start_Item)
        #[arg(long, default_value = "Game_Start_Item")]
        into: String,
        /// Print every entity that can't be moved
        #[arg(long)]
        verbose: bool,
    },
    /// Build every template that has a world instance from the template itself (into one layer, in
    /// memory) and compare the header fields and local box with its first instance
    SpawnCheck {
        /// Target layer (default Game_Start_Item)
        #[arg(long, default_value = "Game_Start_Item")]
        into: String,
        /// Print every mismatch
        #[arg(long)]
        verbose: bool,
    },
    /// Localized strings (data\compiled\strings.pak, the loose data\compiled\strings\<table>.tab wins):
    /// no table = list the tables; else rows whose key or text contains the filter
    Tab {
        /// Table name (focus, items, infos, ...)
        table: Option<String>,
        /// Only rows whose key or shown text contains this (case-insensitive)
        filter: Option<String>,
        /// Columns to show: language codes (de en fr it es pl ru) or column names; default en + ru
        #[arg(long = "lang", value_delimiter = ',')]
        langs: Vec<String>,
        /// Every column
        #[arg(long)]
        all: bool,
        #[arg(long)]
        json: bool,
    },
    /// Set the text of a key (the row is added when missing) and write the loose table
    /// data\compiled\strings\<table>.tab. Item names: focus FO_<template>, descriptions: items ITEMDESC_<template>
    TabSet {
        table: String,
        key: String,
        text: String,
        /// Columns to set: language codes or column names (default: every <Language>_Text column)
        #[arg(long = "lang", value_delimiter = ',')]
        langs: Vec<String>,
        /// Write here instead of the loose game file
        #[arg(long)]
        out: Option<PathBuf>,
        /// Only print what would change
        #[arg(long)]
        dry_run: bool,
    },
    /// Remove a key's row from a table (writes the loose table like `tab-set`)
    TabDelete {
        table: String,
        key: String,
        #[arg(long)]
        out: Option<PathBuf>,
        #[arg(long)]
        dry_run: bool,
    },
    /// Read + write every .tab of data\compiled\strings.pak and data\common\strings.pak, compare bytes
    TabCheck,
    /// Walk every string index of every layer + template (what moving entities between files needs):
    /// unknown value types / class data, unreached table entries, table order, remap test
    StringsCheck {
        /// Print each file with unknown sites or unreached strings
        #[arg(long)]
        files: bool,
    },
}

fn game_dir(arg: Option<PathBuf>) -> Result<PathBuf, String> {
    if let Some(g) = arg.or_else(|| std::env::var_os("RISEN_DIR").map(PathBuf::from)) {
        return Ok(g);
    }
    let cwd = std::env::current_dir().map_err(|e| e.to_string())?;
    cwd.ancestors()
        .find(|d| d.join("data").join("common").join("projects.pak").is_file())
        .map(Path::to_path_buf)
        .ok_or_else(|| "game folder not found: run inside it, or pass --game / set RISEN_DIR".into())
}

fn projects_pak(game: &Path) -> PathBuf {
    game.join("data").join("common").join("projects.pak")
}

fn templates_pak(game: &Path) -> PathBuf {
    game.join("data").join("common").join("templates.pak")
}

/// Template GUID ("{...}", upper case) -> template name, for creator names.
fn template_names(vfs: &GameVfs) -> Result<HashMap<String, String>, Box<dyn std::error::Error>> {
    let mut map = HashMap::new();
    for pe in vfs.files(TEMPLATES, ".tple") {
        let t = Tple::read(&vfs.read(pe)?).map_err(|e| format!("{}: {e}", vfs.source(pe)))?;
        if let Some(e) = t.template() {
            let name = t.name(e).to_string();
            if let Some(old) = map.insert(e.id.text(), name.clone()) {
                return Err(format!("duplicate template GUID {}: {old}, {name} ({})", e.id.text(), pe.path).into());
            }
        }
    }
    Ok(map)
}

/// The template with this GUID ("{...}", upper case) and its mounted virtual path.
fn load_template(vfs: &GameVfs, guid: &str) -> Result<(Tple, String), Box<dyn std::error::Error>> {
    for pe in vfs.files(TEMPLATES, ".tple") {
        let t = Tple::read(&vfs.read(pe)?).map_err(|e| format!("{}: {e}", vfs.source(pe)))?;
        if t.template().is_some_and(|e| e.id.text() == guid) {
            return Ok((t, pe.path.clone()));
        }
    }
    Err(format!("template {guid} not found").into())
}

/// Explicit file or a unique name/GUID in the mounted template catalog (including loose files).
fn template_source(vfs: &GameVfs, spec: &str) -> Result<(Tple, String), Box<dyn std::error::Error>> {
    let path = Path::new(spec);
    if path.is_file() {
        let t = Tple::read(&std::fs::read(path)?).map_err(|e| format!("{spec}: {e}"))?;
        t.template().ok_or("multi-template: not supported")?;
        return Ok((t, spec.to_string()));
    }
    if spec.to_ascii_lowercase().ends_with(".tple") || spec.contains(['/', '\\']) {
        let entry = vfs.find(TEMPLATES, spec)?;
        let t = Tple::read(&vfs.read(entry)?).map_err(|e| format!("{}: {e}", vfs.source(entry)))?;
        t.template().ok_or("multi-template: not supported")?;
        return Ok((t, entry.path.clone()));
    }
    let names = template_names(vfs)?;
    let want = spec.trim_matches(['{', '}']);
    let matches: Vec<_> = names.iter().filter(|(g, n)| {
        n.eq_ignore_ascii_case(want) || g.trim_matches(['{', '}']).eq_ignore_ascii_case(want)
    }).collect();
    let guid = match matches.as_slice() {
        [(guid, _)] => *guid,
        [] => return Err(format!("{spec}: no such template (see `risen templates`)").into()),
        _ => return Err(format!("{spec}: ambiguous template name; use its GUID or file path").into()),
    };
    load_template(vfs, guid)
}

/// Mesh paks for the local bounding box of a new entity built from a template.
struct MeshPaks<'a> {
    vfs: &'a GameVfs,
}

impl<'a> MeshPaks<'a> {
    fn open(vfs: &'a GameVfs) -> Self {
        MeshPaks { vfs }
    }

    /// The `Boundary` of the template's mesh (eCMesh_PS) or animated model (eCAnimation_PS), as world
    /// entities store it; else a 1 m cube. Returns the box and where it came from.
    fn local_box(&self, t: &Tple) -> Result<([f32; 6], String), Box<dyn std::error::Error>> {
        let b = risen_formats::bounds::template_bounds(t, |scope, name| {
            match self.vfs.find(scope, name) {
                Ok(e) => {
                    let bytes = self.vfs.read(e).map_err(|e| e.to_string())?;
                    Ok(risen_formats::resource::boundary(&bytes).map(|b| (b, e.path.clone())))
                }
                Err(VfsError::NotFound(_)) => Ok(None),
                Err(e) => Err(e.to_string()),
            }
        })?;
        let how = format!("{}{}", b.sources.join(" + "), if b.approximate { " (approximate)" } else { "" });
        Ok((b.local_box, how))
    }
}

/// The string tables the game mounts (mountlist_packed.ini lists data/compiled/strings, not data/common/strings;
/// only the compiled copies have Polish + Russian).
fn strings_pak(game: &Path) -> PathBuf {
    game.join("data").join("compiled").join("strings.pak")
}

/// A string table opened for editing: the loose data\compiled\strings\<table>.tab if one exists, else the pak copy.
struct EditTab {
    src: String,
    loose: PathBuf,
    tab: Tab,
    default_writable: bool,
}

fn open_tab_for_edit(vfs: &GameVfs, table: &str) -> Result<EditTab, Box<dyn std::error::Error>> {
    let name = if table.to_lowercase().ends_with(".tab") { table.to_string() } else { format!("{table}.tab") };
    let pe = vfs.find(STRINGS, &name)?;
    let loose = pe.loose.clone();
    let (src, data) = (vfs.source(pe), vfs.read(pe)?);
    let tab = Tab::read(&data).map_err(|e| format!("{src}: {e}"))?;
    Ok(EditTab { src, loose, tab, default_writable: !vfs.no_physical || matches!(pe.source, Source::Loose(_)) })
}

impl EditTab {
    fn column(&self, spec: &str) -> Result<usize, String> {
        self.tab.column(spec).filter(|&c| c > 0).ok_or_else(|| {
            let names: Vec<_> = self.tab.columns.iter().skip(1).map(|c| c.name.as_str()).collect();
            format!("{spec}: no such column (de en fr it es pl ru or {})", names.join(", "))
        })
    }

    fn save(&self, out: Option<PathBuf>) -> Result<(), Box<dyn std::error::Error>> {
        if out.is_none() && !self.default_writable {
            return Err("NoPhysical=true: the game ignores loose overrides; use --out or enable them in the mountlist".into());
        }
        let bytes = self.tab.write();
        Tab::read(&bytes).map_err(|e| format!("written table does not read back: {e}"))?;
        let dest = out.unwrap_or_else(|| self.loose.clone());
        if let Some(dir) = dest.parent() {
            std::fs::create_dir_all(dir)?;
        }
        std::fs::write(&dest, &bytes)?;
        println!("wrote {} ({} bytes)", dest.display(), bytes.len());
        Ok(())
    }
}

/// A layer opened for editing: the loose file if one exists (earlier edits), else the pak copy.
struct EditLayer {
    /// Where it was read from, for messages
    src: String,
    /// Where edits are written (the loose file)
    loose: PathBuf,
    lrent: Lrent,
    default_writable: bool,
}

fn open_layer_for_edit(vfs: &GameVfs, layer: &str) -> Result<EditLayer, Box<dyn std::error::Error>> {
    // A .lrent file on disk is edited in place (unless --out).
    if Path::new(layer).is_file() {
        let lrent = Lrent::read(&std::fs::read(layer)?).map_err(|e| format!("{layer}: {e}"))?;
        return Ok(EditLayer { src: layer.to_string(), loose: PathBuf::from(layer), lrent, default_writable: true });
    }
    let name = if layer.to_lowercase().ends_with(".lrent") { layer.to_string() } else { format!("{layer}.lrent") };
    let pe = vfs.find(PROJECTS, &name)?;
    let loose = pe.loose.clone();
    let (src, data) = (vfs.source(pe), vfs.read(pe)?);
    let lrent = Lrent::read(&data).map_err(|e| format!("{src}: {e}"))?;
    Ok(EditLayer { src, loose, lrent, default_writable: !vfs.no_physical || matches!(pe.source, Source::Loose(_)) })
}

impl EditLayer {
    fn entity_index(&self, spec: &str) -> Result<usize, String> {
        let i = match spec.parse::<usize>() {
            Ok(i) => i,
            Err(_) => self.lrent.find_guid(spec).ok_or_else(|| format!("{spec}: no such entity GUID in {}", self.src))?,
        };
        if i >= self.lrent.entities().len() {
            return Err(format!("index {i}: layer has {} entities", self.lrent.entities().len()));
        }
        Ok(i)
    }

    fn describe(&self, i: usize) -> String {
        let e = &self.lrent.entities()[i];
        let name = self.lrent.strings().get(e.name.0 as usize).cloned().unwrap_or_default();
        format!("[{i}] {name} {} at {:?}", e.id.text(), e.position())
    }

    /// Writes the layer to `out` or the loose file (after checking that it reads back).
    fn save(&self, out: Option<PathBuf>) -> Result<(), Box<dyn std::error::Error>> {
        if out.is_none() && !self.default_writable {
            return Err("NoPhysical=true: the game ignores loose overrides; use --out or enable them in the mountlist".into());
        }
        let bytes = self.lrent.write();
        Lrent::read(&bytes).map_err(|e| format!("written layer does not read back: {e}"))?;
        let dest = out.unwrap_or_else(|| self.loose.clone());
        if let Some(dir) = dest.parent() {
            std::fs::create_dir_all(dir)?;
        }
        std::fs::write(&dest, &bytes)?;
        println!("wrote {} ({} bytes)", dest.display(), bytes.len());
        Ok(())
    }

    /// Copies entity `i` and moves the copy by `d`; returns the copy's index.
    fn copy(&mut self, i: usize, d: [f32; 3]) -> Result<usize, String> {
        let c = self.lrent.context_mut().ok_or("layer has no entity context")?;
        let n = c.duplicate(i)?;
        self.lrent.translate(n, d)?;
        Ok(n)
    }

    /// Copies entity `i` of another layer into this one and moves the copy by `d`; returns the
    /// copy's index. With `warn`, prints the source-layer entities the copy still references.
    fn copy_from(&mut self, src: &EditLayer, i: usize, d: [f32; 3], warn: bool) -> Result<usize, String> {
        let imp = self.lrent.import(&src.lrent, &[i])?.remove(0);
        if warn {
            for g in &imp.outside_refs {
                let text = risen_formats::props::format_guid(g, true);
                let what = src.lrent.find_guid(&text).map_or(text, |j| src.describe(j));
                println!("  NOTE the copy references {what} (stays in the source layer)");
            }
        }
        self.lrent.translate(imp.index, d)?;
        Ok(imp.index)
    }
}

/// Loads a layer from a file path, or from projects.pak by name / path (".lrent" optional).
fn load_layer(vfs: Option<&GameVfs>, spec: &str) -> Result<(String, Lrent), Box<dyn std::error::Error>> {
    let p = Path::new(spec);
    let data = if p.is_file() {
        (spec.to_string(), std::fs::read(p)?)
    } else {
        let l = open_layer_for_edit(vfs.ok_or("game folder is needed to find a layer by name")?, spec)?;
        return Ok((l.src, l.lrent));
    };
    Ok((data.0, Lrent::read(&data.1)?))
}

#[derive(Serialize)]
struct PropOut {
    name: String,
    #[serde(rename = "type")]
    ty: String,
    value: String,
}

#[derive(Serialize)]
struct SetOut {
    class: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    props: Option<Vec<PropOut>>,
}

#[derive(Serialize)]
struct EntityOut {
    #[serde(skip_serializing_if = "Option::is_none")]
    layer: Option<String>,
    index: usize,
    /// Parent entity index when it is not the layer root (0)
    #[serde(skip_serializing_if = "Option::is_none")]
    parent: Option<usize>,
    name: String,
    guid: String,
    position: [f32; 3],
    creator: Option<String>,
    /// Name of the creator template (mounted catalog)
    template: Option<String>,
    sets: Vec<SetOut>,
    #[serde(skip_serializing_if = "Option::is_none")]
    geometry: Option<GeoOut>,
}

#[derive(Serialize)]
struct GeoOut {
    local_matrix: [f32; 16],
    world_matrix: [f32; 16],
    local_box: [f32; 6],
    world_box: [f32; 6],
    world_sphere: [f32; 4],
    geo_box: [f32; 6],
    geo_sphere: [f32; 4],
}

fn geo_out(e: &DynamicEntity) -> GeoOut {
    GeoOut {
        local_matrix: e.local_matrix,
        world_matrix: e.world_matrix,
        local_box: e.local_box,
        world_box: e.world_box,
        world_sphere: e.world_sphere,
        geo_box: e.geo_box,
        geo_sphere: e.geo_sphere,
    }
}

fn set_out(o: &PropertyObject, s: &[String], with_props: bool) -> SetOut {
    SetOut {
        class: o.class(s).to_string(),
        props: with_props.then(|| {
            o.body.as_ref().map_or(Vec::new(), |b| {
                b.props
                    .list
                    .iter()
                    .map(|p| {
                        let ty = s.get(p.ty.0 as usize).cloned().unwrap_or_default();
                        PropOut {
                            name: s.get(p.name.0 as usize).cloned().unwrap_or_default(),
                            value: format_value(&ty, &p.value, s),
                            ty,
                        }
                    })
                    .collect()
            })
        }),
    }
}

fn entity_out(
    i: usize,
    e: &DynamicEntity,
    s: &[String],
    with_props: bool,
    templates: &HashMap<String, String>,
) -> EntityOut {
    use risen_formats::lrent::Creator;
    let creator = match &e.creator {
        Creator::Proxy(p) => p.id.map(|id| id.text()),
        Creator::Old { id, .. } => id.map(|id| id.text()),
    };
    EntityOut {
        layer: None,
        index: i,
        parent: None,
        name: s.get(e.name.0 as usize).cloned().unwrap_or_default(),
        guid: e.id.text(),
        position: e.position(),
        template: creator.as_ref().and_then(|g| templates.get(g).cloned()),
        creator,
        sets: e.property_sets.iter().map(|ps| set_out(&ps.object, s, with_props)).collect(),
        geometry: None,
    }
}

fn print_entities(list: &[EntityOut]) {
    for e in list {
        if let Some(layer) = &e.layer {
            println!("{layer} [{}] {}", e.index, e.guid);
        }
        let [x, y, z] = e.position;
        let classes: Vec<_> = e.sets.iter().map(|s| s.class.as_str()).collect();
        println!("{:4} {:32} {:>10.0} {:>8.0} {:>10.0}  {}", e.index, e.name, x, y, z, classes.join(" "));
        if let Some(p) = e.parent {
            println!("       child of [{p}]");
        }
        if let Some(t) = e.template.as_ref().filter(|t| **t != e.name) {
            println!("       template {t}");
        }
        for p in e.sets.iter().flat_map(|s| s.props.iter().flatten().map(move |p| (&s.class, p))) {
            println!("       {}.{} = {}", p.0, p.1.name, p.1.value);
        }
        if let Some(g) = &e.geometry {
            println!("       local  {:?}", g.local_matrix);
            println!("       world  {:?}", g.world_matrix);
            println!("       boxes  local {:?} world {:?} geo {:?}", g.local_box, g.world_box, g.geo_box);
            println!("       sphere world {:?} geo {:?}", g.world_sphere, g.geo_sphere);
        }
    }
}

fn run(cli: Cli) -> Result<(), Box<dyn std::error::Error>> {
    match cli.cmd {
        Cmd::Vfs { filter, json } => {
            let vfs = GameVfs::open(game_dir(cli.game)?)?;
            #[derive(Serialize)]
            struct FileOut { path: String, size: u64, source: String }
            let filter = filter.map(|s| s.to_ascii_lowercase());
            let files: Vec<_> = vfs.entries.iter().filter(|e| {
                filter.as_ref().is_none_or(|s| e.path.to_ascii_lowercase().contains(s))
            }).map(|e| FileOut { path: e.path.clone(), size: e.size, source: vfs.source(e) }).collect();
            if json {
                println!("{}", serde_json::to_string_pretty(&files)?);
            } else {
                for e in &files { println!("{:9} {} <- {}", e.size, e.path, e.source); }
            }
            eprintln!("{} files shown, {} mounted, {} archives; NoPhysical={} NoPacked={}",
                files.len(), vfs.entries.len(), vfs.archive_count(), vfs.no_physical, vfs.no_packed);
        }
        Cmd::PakList { pak, filter } => {
            let pak = Pak::open(&pak)?;
            let filter = filter.map(|f| f.to_lowercase());
            for e in &pak.entries {
                if filter.as_ref().is_none_or(|f| e.path.to_lowercase().contains(f)) {
                    println!("{:10} {}{} {}", e.stored_size, e.encryption, e.compression, e.path);
                }
            }
            eprintln!("total {}", pak.entries.len());
        }
        Cmd::PakExtract { pak, name, out } => {
            let pak = Pak::open(&pak)?;
            let e = pak.find(&name).ok_or_else(|| format!("{name}: not in {}", pak.path.display()))?;
            let data = pak.read(e)?;
            std::fs::write(&out, &data)?;
            println!("wrote {} bytes to {}", data.len(), out.display());
        }
        Cmd::Layers { filter } => {
            let vfs = GameVfs::open(game_dir(cli.game)?)?;
            let filter = filter.map(|f| f.to_lowercase());
            for e in vfs.files(PROJECTS, ".lrent") {
                if filter.as_ref().is_none_or(|f| e.path.to_lowercase().contains(f)) {
                    println!("{:9} {}", e.size, relative_asset(&e.path, PROJECTS));
                }
            }
        }
        Cmd::Projects { filter, json } => {
            #[derive(Serialize)]
            struct ProjectOut {
                path: String,
                source: String,
                class: String,
                version: u16,
                enabled: Option<u8>,
                layers: Vec<String>,
                world_data_file: Option<String>,
            }
            let vfs = GameVfs::open(game_dir(cli.game)?)?;
            let filter = filter.map(|f| f.to_lowercase());
            let mut list = Vec::new();
            for e in vfs.files(PROJECTS, ".wrl").chain(vfs.files(PROJECTS, ".sec")) {
                if filter.as_ref().is_some_and(|f| !e.path.to_lowercase().contains(f)) { continue; }
                let p = ProjectFile::read(&vfs.read(e)?).map_err(|err| format!("{}: {err}", vfs.source(e)))?;
                let s = p.sector();
                let w = p.world();
                list.push(ProjectOut {
                    path: relative_asset(&e.path, PROJECTS),
                    source: vfs.source(e),
                    class: p.root.class(&p.archive.strings).to_string(),
                    version: s.map(|s| s.version).or_else(|| w.map(|w| w.version)).unwrap(),
                    enabled: s.and_then(|s| s.enabled),
                    layers: s.map(|s| s.layers.iter().map(|&r| format!("{}.lrent", p.archive.str(r))).collect()).unwrap_or_default(),
                    world_data_file: w.and_then(|w| w.world_data_file).map(|r| p.archive.str(r).to_string()),
                });
            }
            if json {
                println!("{}", serde_json::to_string_pretty(&list)?);
            } else {
                for p in list {
                    println!("{}  {} v{}{}", p.path, p.class, p.version,
                        p.enabled.map(|v| format!(" enabled={v}")).unwrap_or_default());
                    if let Some(file) = p.world_data_file { println!("  world data: {file}"); }
                    for layer in p.layers { println!("  {layer}"); }
                }
            }
        }
        Cmd::ProjectsCheck { pak } => {
            let pak = pak.map_or_else(|| game_dir(cli.game).map(|g| projects_pak(&g)), Ok)?;
            for ext in [".sec", ".wrl"] {
                println!("{ext}:");
                round_trip_check(&pak, ext, |d| ProjectFile::read(d).map(|p| {
                    let n = p.sector().map_or(1, |s| s.layers.len());
                    (p.write(), n)
                }))?;
            }
        }
        Cmd::WorldSectors { world, json } => {
            #[derive(Serialize)]
            struct SectorOut<'a> { name: &'a str, enabled: bool }
            let data = if Path::new(&world).is_file() && world.to_ascii_lowercase().ends_with(".wrldatasc") {
                WorldData::read(&std::fs::read(&world)?)?
            } else {
                let vfs = GameVfs::open(game_dir(cli.game)?)?;
                let path = if world.to_ascii_lowercase().ends_with(".wrldatasc") { world.clone() } else {
                    let name = if world.to_ascii_lowercase().ends_with(".wrl") { world.clone() } else { format!("{world}.wrl") };
                    let p = ProjectFile::read(&vfs.read(vfs.find(PROJECTS, &name)?)?)?;
                    let r = p.world().and_then(|w| w.world_data_file).ok_or("world has no world-data filename")?;
                    p.archive.str(r).to_string()
                };
                WorldData::read(&vfs.read(vfs.find(PROJECTS, &path)?)?)?
            };
            if json {
                let out: Vec<_> = data.sectors().iter().map(|s| SectorOut { name: &s.name, enabled: s.enabled }).collect();
                println!("{}", serde_json::to_string_pretty(&out)?);
            } else {
                for s in data.sectors() { println!("{}={}", s.name, s.enabled); }
            }
        }
        Cmd::WorldDataCheck { pak } => {
            let pak = pak.map_or_else(|| game_dir(cli.game).map(|g| projects_pak(&g)), Ok)?;
            round_trip_check(&pak, ".wrldatasc", |d| WorldData::read(d).map(|w| (w.write(), w.sectors().len())))?;
        }
        Cmd::LayerCreate { name, world, like, spawn, to, out_dir, dry_run } => {
            let vfs = GameVfs::open(game_dir(cli.game)?)?;
            let mut changes = project::plan(&vfs, &name, &world, &like)?;
            if let Some(template) = spawn {
                let pos: [f32; 3] = to.ok_or("--spawn needs --to")?.try_into().map_err(|_| "--to needs 3 coordinates")?;
                if !pos.iter().all(|v| v.is_finite()) { return Err("--to coordinates must be finite".into()); }
                let (t, path) = template_source(&vfs, &template)?;
                let (bounds, how) = MeshPaks::open(&vfs).local_box(&t)?;
                let change = changes.iter_mut().find(|c| c.path.ends_with(".lrent")).ok_or("no new layer")?;
                let mut l = Lrent::read(&change.bytes)?;
                let n = l.spawn_template(&t, pos, bounds)?;
                change.bytes = l.write();
                Lrent::read(&change.bytes)?;
                println!("spawn {path} [{n}] at {pos:?}, bounds from {how}");
            }
            for c in &changes { println!("{} {} ({} bytes)", if c.replace { "update" } else { "create" }, c.path, c.bytes.len()); }
            if !dry_run {
                if out_dir.is_none() && vfs.no_physical { return Err("NoPhysical=true: enable loose files or use --out-dir".into()); }
                let root = out_dir.unwrap_or_else(|| vfs.root.clone());
                std::fs::create_dir_all(&root)?;
                let backup = project::apply(&root, &changes)?;
                println!("BACKUP: {}", backup.display());
                println!("RESTORE: risen world-restore \"{}\"", backup.display());
            }
        }
        Cmd::WorldRestore { backup, force } => {
            project::restore(&backup, force)?;
            println!("restored {}", backup.display());
        }
        Cmd::Physics { layer, entity, mode, out, dry_run } => {
            let vfs = GameVfs::open(game_dir(cli.game)?)?;
            geometry::physics(&vfs, &layer, &entity, &mode, out, dry_run)?;
        }
        Cmd::Climbable { layer, entity, enabled, out, dry_run } => {
            let vfs = GameVfs::open(game_dir(cli.game)?)?;
            geometry::climbable(&vfs, &layer, &entity, enabled, out, dry_run)?;
        }
        Cmd::Ground { layer, entity, collision, height, align, samples, max_slope, surface_layer, world, max_drop, max_rise, gap, out, dry_run } => {
            let vfs = GameVfs::open(game_dir(cli.game)?)?;
            geometry::ground(&vfs, &layer, &entity, collision, height, align, samples.map(usize::from), max_slope, surface_layer.as_deref(), &world, max_drop, max_rise, gap, out, dry_run)?;
        }
        Cmd::Snap { layer, entity, against, target_layer, side, gap, center, out, dry_run } => {
            let vfs = GameVfs::open(game_dir(cli.game)?)?;
            geometry::snap(&vfs, &layer, &entity, &against, target_layer.as_deref(), &side, gap, center, out, dry_run)?;
        }
        Cmd::Geometry { source, entity, json, obj } => {
            let vfs = GameVfs::open(game_dir(cli.game)?)?;
            geometry::inspect(&vfs, &source, entity.as_deref(), json, obj.as_deref())?;
        }
        Cmd::Entities { layer, props, geometry, json } => {
            let vfs = match game_dir(cli.game) {
                Ok(game) => Some(GameVfs::open(game)?),
                Err(_) if Path::new(&layer).is_file() => None,
                Err(e) => return Err(e.into()),
            };
            let templates = vfs.as_ref().map(template_names).transpose()?.unwrap_or_default();
            let (path, l) = load_layer(vfs.as_ref(), &layer)?;
            let s = l.strings();
            let list: Vec<_> = l
                .entities()
                .iter()
                .enumerate()
                .map(|(i, e)| EntityOut {
                    geometry: geometry.then(|| geo_out(e)),
                    parent: parent_of(&l, i),
                    ..entity_out(i, e, s, props, &templates)
                })
                .collect();
            if json {
                println!("{}", serde_json::to_string_pretty(&list)?);
                return Ok(());
            }
            println!("{path}: {} entities", list.len());
            print_entities(&list);
        }
        Cmd::Find { text, props, json } => {
            let game = game_dir(cli.game)?;
            let vfs = GameVfs::open(&game)?;
            let templates = template_names(&vfs)?;
            let needle = text.to_lowercase();
            let needle = needle.trim_matches(['{', '}']);
            let mut list = Vec::new();
            for pe in vfs.files(PROJECTS, ".lrent") {
                let l = Lrent::read(&vfs.read(pe)?).map_err(|e| format!("{}: {e}", vfs.source(pe)))?;
                let s = l.strings();
                for (i, e) in l.entities().iter().enumerate() {
                    let name = s.get(e.name.0 as usize).map_or(String::new(), |n| n.to_lowercase());
                    let guid = e.id.text().to_lowercase();
                    if name.contains(needle) || guid.trim_matches(['{', '}']) == needle {
                        let mut o = entity_out(i, e, s, props, &templates);
                        o.parent = parent_of(&l, i);
                        o.layer = Some(relative_asset(&pe.path, PROJECTS));
                        list.push(o);
                    }
                }
            }
            if json {
                println!("{}", serde_json::to_string_pretty(&list)?);
            } else {
                print_entities(&list);
                eprintln!("{} found", list.len());
            }
        }
        Cmd::Move { layer, entity, to, by, out, dry_run } => {
            let game = game_dir(cli.game)?;
            let vfs = GameVfs::open(&game)?;
            let mut l = open_layer_for_edit(&vfs, &layer)?;
            let i = l.entity_index(&entity)?;
            let from = l.lrent.entities()[i].position();
            let d = match (to, by) {
                (Some(t), _) => [t[0] - from[0], t[1] - from[1], t[2] - from[2]],
                (None, Some(b)) => [b[0], b[1], b[2]],
                (None, None) => unreachable!(),
            };
            println!("{}", l.src);
            println!("move {}", l.describe(i));
            l.lrent.translate(i, d)?;
            println!("  -> {:?}", l.lrent.entities()[i].position());
            if !dry_run {
                l.save(out)?;
            }
        }
        Cmd::Copy { layer, entity, into, to, by, count, out, dry_run } => {
            let game = game_dir(cli.game)?;
            let vfs = GameVfs::open(&game)?;
            let mut l = open_layer_for_edit(&vfs, &layer)?;
            let i = l.entity_index(&entity)?;
            let from = l.lrent.entities()[i].position();
            println!("{}", l.src);
            println!("copy {}", l.describe(i));
            // --into the same layer = an ordinary copy.
            let mut target = match &into {
                Some(t) => Some(open_layer_for_edit(&vfs, t)?).filter(|t| !same_file(&t.loose, &l.loose)),
                None => None,
            };
            if let Some(t) = &target {
                println!("into {}", t.src);
            }
            for k in 1..=count {
                let d = match (&to, &by) {
                    (Some(t), _) => [t[0] - from[0], t[1] - from[1], t[2] - from[2]],
                    (None, Some(b)) => [b[0] * k as f32, b[1] * k as f32, b[2] * k as f32],
                    (None, None) => [0.0; 3],
                };
                match &mut target {
                    Some(t) => {
                        let n = t.copy_from(&l, i, d, k == 1)?;
                        println!("  -> {}", t.describe(n));
                    }
                    None => {
                        let n = l.copy(i, d)?;
                        println!("  -> {}", l.describe(n));
                    }
                }
            }
            if !dry_run {
                target.as_ref().unwrap_or(&l).save(out)?;
            }
        }
        Cmd::Spawn { template, to, yaw, layer, count, spread, from_template, out, dry_run } => {
            if !to.iter().all(|v| v.is_finite()) || !spread.is_finite() {
                return Err("spawn coordinates and spread must be finite".into());
            }
            if yaw.is_some_and(|v| !v.is_finite()) { return Err("yaw must be finite".into()); }
            if let Some(yaw) = yaw { println!("yaw {yaw} degrees (world heading: 0 = +Z, 90 = +X)"); }
            let game = game_dir(cli.game)?;
            let vfs = GameVfs::open(&game)?;
            let (t, template_path) = template_source(&vfs, &template)?;
            let guid = t.template().ok_or("multi-template: not supported")?.id.text();
            // Source instance (top-level, i.e. direct child of the layer root): from the target
            // layer when it has one, else from the layer with the most instances.
            let target = layer.as_ref().map(|t| open_layer_for_edit(&vfs, t)).transpose()?;
            // Position of copy k: on a circle around --to.
            let place = |k: usize| {
                let a = k as f32 / count as f32 * std::f32::consts::TAU;
                let r = if count > 1 { spread } else { 0.0 };
                [to[0] + r * a.cos(), to[1], to[2] + r * a.sin()]
            };
            let mut source = None;
            let mut from_template = from_template || Path::new(&template).is_file();
            if !from_template && target.as_ref().is_none_or(|t| instances(&t.lrent, &guid).is_empty()) {
                let mut best: Option<(usize, String)> = None;
                for pe in vfs.files(PROJECTS, ".lrent") {
                    let l = Lrent::read(&vfs.read(pe)?).map_err(|e| format!("{}: {e}", vfs.source(pe)))?;
                    let n = instances(&l, &guid).len();
                    if n > 0 && best.as_ref().is_none_or(|b| n > b.0) {
                        best = Some((n, pe.path.clone()));
                    }
                }
                match best {
                    Some((_, path)) => {
                        let s = open_layer_for_edit(&vfs, &path)?;
                        // (The target's loose file may have lost the instances its pak copy has.)
                        if target.as_ref().is_none_or(|t| !same_file(&t.loose, &s.loose)) {
                            source = Some(s);
                        }
                    }
                    None => {
                        println!("no top-level instance of {template} in the world: building it from the template");
                        from_template = true;
                    }
                }
            }
            if from_template {
                let mut l = target.ok_or("--layer is needed to spawn from the template")?;
                let (local_box, how) = MeshPaks::open(&vfs).local_box(&t)?;
                println!("template {template_path}, local box from {how}");
                println!("into {}", l.src);
                for k in 0..count {
                    let n = l.lrent.spawn_template(&t, place(k), local_box)?;
                    if let Some(yaw) = yaw { l.lrent.set_yaw(n, yaw)?; }
                    println!("  -> {}", l.describe(n));
                }
                if !dry_run {
                    l.save(out)?;
                }
                return Ok(());
            }
            let mut l = match target {
                Some(t) => t,
                None => source.take().ok_or("no layer")?,
            };
            let src = source.as_ref().unwrap_or(&l);
            let i = *instances(&src.lrent, &guid).first().ok_or("no instance left in the loose file")?;
            let from = src.lrent.entities()[i].position();
            println!("source {}: {}", src.src, src.describe(i));
            if source.is_some() {
                println!("into {}", l.src);
            }
            for k in 0..count {
                let p = place(k);
                let d = [p[0] - from[0], p[1] - from[1], p[2] - from[2]];
                let n = match &source {
                    Some(s) => l.copy_from(s, i, d, k == 0)?,
                    None => l.copy(i, d)?,
                };
                if let Some(yaw) = yaw { l.lrent.set_yaw(n, yaw)?; }
                println!("  -> {}", l.describe(n));
            }
            if !dry_run {
                l.save(out)?;
            }
        }
        Cmd::Delete { layer, entity, out, dry_run } => {
            let game = game_dir(cli.game)?;
            let vfs = GameVfs::open(&game)?;
            let mut l = open_layer_for_edit(&vfs, &layer)?;
            let i = l.entity_index(&entity)?;
            println!("{}", l.src);
            println!("delete {}", l.describe(i));
            let c = l.lrent.context_mut().ok_or("layer has no entity context")?;
            let removed = c.remove(i)?;
            if removed.len() > 1 {
                println!("  with {} child entities", removed.len() - 1);
            }
            // Other places that still mention a removed GUID (the bytes of a bCGuid / entity proxy).
            let this_layer = l.lrent.write();
            for e in &removed {
                let mut hits = Vec::new();
                if count_bytes(&this_layer, &e.id.guid) > 0 {
                    hits.push("this layer".to_string());
                }
                for pe in vfs.files(PROJECTS, ".lrent") {
                    if same_file(&pe.loose, &l.loose) {
                        continue;
                    }
                    let data = vfs.read(pe)?;
                    if count_bytes(&data, &e.id.guid) > 0 {
                        hits.push(pe.path.clone());
                    }
                }
                if !hits.is_empty() {
                    println!("  WARNING {} is still referenced in: {}", e.id.text(), hits.join(", "));
                }
            }
            if !dry_run {
                l.save(out)?;
            }
        }
        Cmd::LrentCheck { pak } => {
            let pak = pak.map_or_else(|| game_dir(cli.game).map(|g| projects_pak(&g)), Ok)?;
            round_trip_check(&pak, ".lrent", |d| Lrent::read(d).map(|l| (l.write(), l.entities().len())))?;
        }
        Cmd::TpleCheck { pak } => {
            let pak = pak.map_or_else(|| game_dir(cli.game).map(|g| templates_pak(&g)), Ok)?;
            round_trip_check(&pak, ".tple", |d| Tple::read(d).map(|t| (t.write(), t.entities.len())))?;
        }
        Cmd::CopyCheck { into, verbose } => {
            let pak = Pak::open(projects_pak(&game_dir(cli.game)?))?;
            let name = if into.to_lowercase().ends_with(".lrent") { into.clone() } else { format!("{into}.lrent") };
            let pe = pak.find(&name).ok_or_else(|| format!("{name}: not in projects.pak"))?;
            let target = Lrent::read(&pak.read(pe)?)?;
            copy_check(&pak, &target, verbose)?;
        }
        Cmd::SpawnCheck { into, verbose } => {
            let game = game_dir(cli.game)?;
            spawn_check(&game, &into, verbose)?;
        }
        Cmd::Tab { table: None, .. } => {
            let vfs = GameVfs::open(game_dir(cli.game)?)?;
            for e in vfs.files(STRINGS, ".tab") {
                let t = Tab::read(&vfs.read(e)?).map_err(|err| format!("{}: {err}", vfs.source(e)))?;
                let cols: Vec<_> = t.columns.iter().map(|c| c.name.as_str()).collect();
                println!("{:14} {:6} rows  {}", relative_asset(&e.path, STRINGS).trim_end_matches(".tab"), t.rows(), cols.join(" "));
            }
        }
        Cmd::Tab { table: Some(table), filter, langs, all, json } => {
            let vfs = GameVfs::open(game_dir(cli.game)?)?;
            let t = open_tab_for_edit(&vfs, &table)?;
            let cols: Vec<usize> = if all {
                (1..t.tab.columns.len()).collect()
            } else {
                let langs = if langs.is_empty() { vec!["en".to_string(), "ru".to_string()] } else { langs };
                langs.iter().map(|l| t.column(l)).collect::<Result<_, _>>()?
            };
            let want = filter.map(|f| f.to_lowercase());
            let keys = &t.tab.columns[0].rows;
            let rows: Vec<usize> = (0..t.tab.rows())
                .filter(|&i| {
                    want.as_ref().is_none_or(|w| {
                        keys[i].to_lowercase().contains(w)
                            || cols.iter().any(|&c| t.tab.columns[c].rows[i].to_lowercase().contains(w))
                    })
                })
                .collect();
            if json {
                let out: Vec<BTreeMap<&str, &str>> = rows
                    .iter()
                    .map(|&i| {
                        std::iter::once(0)
                            .chain(cols.iter().copied())
                            .map(|c| (t.tab.columns[c].name.as_str(), t.tab.columns[c].rows[i].as_str()))
                            .collect()
                    })
                    .collect();
                println!("{}", serde_json::to_string_pretty(&out)?);
            } else {
                println!("{}", t.src);
                for &i in &rows {
                    println!("{}", keys[i]);
                    for &c in &cols {
                        println!("  {:16} {}", t.tab.columns[c].name, t.tab.columns[c].rows[i]);
                    }
                }
                println!("{} of {} rows", rows.len(), t.tab.rows());
            }
        }
        Cmd::TabSet { table, key, text, langs, out, dry_run } => {
            let vfs = GameVfs::open(game_dir(cli.game)?)?;
            let mut t = open_tab_for_edit(&vfs, &table)?;
            let cols: Vec<usize> = if langs.is_empty() {
                t.tab.text_columns()
            } else {
                langs.iter().map(|l| t.column(l)).collect::<Result<_, _>>()?
            };
            println!("{}", t.src);
            let (row, added) = t.tab.row_or_add(&key);
            println!("{} {}", if added { "add" } else { "set" }, t.tab.columns[0].rows[row]);
            for c in cols {
                let col = &mut t.tab.columns[c];
                println!("  {:16} {:?} -> {:?}", col.name, col.rows[row], text);
                col.rows[row] = text.clone();
            }
            if !dry_run {
                t.save(out)?;
            }
        }
        Cmd::TabDelete { table, key, out, dry_run } => {
            let vfs = GameVfs::open(game_dir(cli.game)?)?;
            let mut t = open_tab_for_edit(&vfs, &table)?;
            println!("{}", t.src);
            if !t.tab.remove(&key) {
                return Err(format!("{key}: no such key in {table}").into());
            }
            println!("removed {key}");
            if !dry_run {
                t.save(out)?;
            }
        }
        Cmd::TabCheck => {
            let game = game_dir(cli.game)?;
            for pak in [strings_pak(&game), game.join("data").join("common").join("strings.pak")] {
                println!("{}", pak.display());
                round_trip_check(&pak, ".tab", |d| Tab::read(d).map(|t| (t.write(), t.rows())))?;
            }
        }
        Cmd::StringsCheck { files } => {
            let game = game_dir(cli.game)?;
            let mut st = StrCheck::default();
            st.pak::<Lrent>(&projects_pak(&game), ".lrent", files)?;
            st.pak::<Tple>(&templates_pak(&game), ".tple", files)?;
            st.report();
            if st.remap_failed > 0 || !st.complete_not_exact.is_empty() {
                return Err("strings check failed".into());
            }
        }
        Cmd::TemplateCopy { template, name, out, dry_run } => {
            let game = game_dir(cli.game)?;
            let vfs = GameVfs::open(&game)?;
            let (src, path) = template_source(&vfs, &template)?;
            let t = src.copy_named(&name)?;
            if out.is_none() && vfs.no_physical && !Path::new(&template).is_file() && !dry_run {
                return Err("NoPhysical=true: the game ignores loose templates; use --out or enable them in the mountlist".into());
            }
            if template_names(&vfs)?.values().any(|n| n.eq_ignore_ascii_case(&name)) {
                return Err(format!("{name}: a template with this name already exists in mounted assets").into());
            }
            let dest = out.unwrap_or_else(|| {
                let source = if Path::new(&template).is_file() {
                    PathBuf::from(&template)
                } else {
                    vfs.find(TEMPLATES, &path).expect("loaded template is in VFS").loose.clone()
                };
                source.with_file_name(format!("{name}.tple"))
            });
            if dest.exists() {
                return Err(format!("{}: already exists; choose a different --out", dest.display()).into());
            }
            println!("source {path}: {} {}", src.name(src.template().unwrap()), src.template().unwrap().id.text());
            println!("  -> {name} {}", t.template().unwrap().id.text());
            println!("  new text keys: focus FO_{name}, items ITEMDESC_{name}");
            let bytes = t.write();
            let back = Tple::read(&bytes).map_err(|e| format!("copied template does not read back: {e}"))?;
            if back.write() != bytes {
                return Err("copied template does not round-trip".into());
            }
            if dry_run {
                println!("would write {} ({} bytes)", dest.display(), bytes.len());
            } else {
                if let Some(dir) = dest.parent().filter(|p| !p.as_os_str().is_empty()) {
                    std::fs::create_dir_all(dir)?;
                }
                use std::io::Write;
                std::fs::OpenOptions::new().write(true).create_new(true).open(&dest)?.write_all(&bytes)?;
                println!("wrote {} ({} bytes)", dest.display(), bytes.len());
            }
        }
        Cmd::Templates { filter, props, json } => {
            let vfs = GameVfs::open(game_dir(cli.game)?)?;
            let needle = filter.map(|f| f.to_lowercase().trim_matches(['{', '}']).to_string());
            let mut list = Vec::new();
            for pe in vfs.files(TEMPLATES, ".tple") {
                let t = Tple::read(&vfs.read(pe)?).map_err(|e| format!("{}: {e}", vfs.source(pe)))?;
                // Multi-templates are listed by their root entity (named like the file).
                let Some(e) = t.template().or(t.entities.first()) else {
                    continue;
                };
                let (name, guid) = (t.name(e).trim_end_matches(".tple").to_string(), e.id.text());
                let hit = needle.as_ref().is_none_or(|n| {
                    name.to_lowercase().contains(n)
                        || pe.path.to_lowercase().contains(n)
                        || guid.to_lowercase().trim_matches(['{', '}']) == n
                });
                if hit {
                    let s = t.strings();
                    list.push(TemplateOut {
                        name,
                        guid,
                        path: relative_asset(&pe.path, TEMPLATES),
                        multi: t.is_multi().then(|| t.entities.len() - 1),
                        sets: e.sets.iter().map(|ps| set_out(&ps.object, s, props)).collect(),
                    });
                }
            }
            if json {
                println!("{}", serde_json::to_string_pretty(&list)?);
                return Ok(());
            }
            for t in &list {
                let classes: Vec<_> = t.sets.iter().map(|s| s.class.as_str()).collect();
                let multi = t.multi.map(|n| format!("multi-template, {n} instances")).unwrap_or_default();
                println!("{:40} {}  {}{multi}", t.name, t.guid, classes.join(" "));
                println!("       {}", t.path);
                for p in t.sets.iter().flat_map(|s| s.props.iter().flatten().map(move |p| (&s.class, p))) {
                    println!("       {}.{} = {}", p.0, p.1.name, p.1.value);
                }
            }
            eprintln!("{} templates", list.len());
        }
    }
    Ok(())
}

fn parent_of(l: &Lrent, i: usize) -> Option<usize> {
    l.context().and_then(|c| c.parent(i)).filter(|&p| p != 0)
}

fn count_bytes(hay: &[u8], needle: &[u8]) -> usize {
    hay.windows(needle.len()).filter(|w| *w == needle).count()
}

/// Indices of the top-level entities of `l` created from template `guid`.
fn instances(l: &Lrent, guid: &str) -> Vec<usize> {
    use risen_formats::lrent::Creator;
    let c = l.context();
    l.entities()
        .iter()
        .enumerate()
        .filter(|(i, e)| {
            let creator = match &e.creator {
                Creator::Proxy(p) => p.id,
                Creator::Old { id, .. } => *id,
            };
            creator.is_some_and(|id| id.text() == guid) && c.is_some_and(|c| c.parent(*i) == Some(0))
        })
        .map(|(i, _)| i)
        .collect()
}

#[derive(Serialize)]
struct TemplateOut {
    name: String,
    guid: String,
    path: String,
    /// Multi-template (prefab): number of template instances in it
    #[serde(skip_serializing_if = "Option::is_none")]
    multi: Option<usize>,
    sets: Vec<SetOut>,
}

/// See `Cmd::SpawnCheck`.
fn spawn_check(game: &Path, into: &str, verbose: bool) -> Result<(), Box<dyn std::error::Error>> {
    use risen_formats::lrent::Creator;
    let pak = Pak::open(projects_pak(game))?;
    let tpak = Pak::open(templates_pak(game))?;
    let vfs = GameVfs::open(game)?;
    let mesh = MeshPaks::open(&vfs);
    let mut templates: HashMap<[u8; 16], Tple> = HashMap::new();
    for pe in tpak.entries.iter().filter(|e| e.path.to_lowercase().ends_with(".tple")) {
        let t = Tple::read(&tpak.read(pe)?).map_err(|e| format!("{}: {e}", pe.path))?;
        if let Some(e) = t.template() {
            templates.insert(e.id.guid, t.clone());
        }
    }
    let name = if into.to_lowercase().ends_with(".lrent") { into.to_string() } else { format!("{into}.lrent") };
    let pe = pak.find(&name).ok_or_else(|| format!("{name}: not in projects.pak"))?;
    let mut target = Lrent::read(&pak.read(pe)?)?;
    let mut seen = std::collections::HashSet::new();
    let mut same: BTreeMap<&str, (usize, usize)> = BTreeMap::new();
    let (mut built, mut refused) = (0, BTreeMap::<String, usize>::new());
    for pe in pak.entries.iter().filter(|e| e.path.to_lowercase().ends_with(".lrent")) {
        let l = Lrent::read(&pak.read(pe)?).map_err(|e| format!("{}: {e}", pe.path))?;
        let Some(c) = l.context() else { continue };
        for (i, e) in c.entities.iter().enumerate().skip(1) {
            let Creator::Proxy(p) = &e.creator else { continue };
            let Some(id) = p.id else { continue };
            let Some(t) = templates.get(&id.guid) else { continue };
            if c.parent(i) != Some(0) || !seen.insert(id.guid) {
                continue;
            }
            let (local_box, _) = mesh.local_box(t)?;
            let n = match target.spawn_template(t, e.position(), local_box) {
                Ok(n) => n,
                Err(why) => {
                    *refused.entry(why).or_default() += 1;
                    continue;
                }
            };
            built += 1;
            let (s, ts) = (l.strings(), target.strings());
            let b = &target.entities()[n];
            let close = |a: &[f32], b: &[f32]| a.iter().zip(b).all(|(x, y)| (x - y).abs() <= 0.01 * (1.0 + x.abs()));
            let checks: [(&str, bool); 14] = [
                ("name", s.get(e.name.0 as usize) == ts.get(b.name.0 as usize)),
                (
                    "versions",
                    (e.version, e.geo_version, e.entity_version, e.node_version)
                        == (b.version, b.geo_version, b.entity_version, b.node_version),
                ),
                ("enabled/rendering", (e.enabled, e.rendering_enabled) == (b.enabled, b.rendering_enabled)),
                ("picking/collision", (e.picking_enabled, e.collision_enabled) == (b.picking_enabled, b.collision_enabled)),
                ("unknown_b1", e.unknown_b1 == b.unknown_b1),
                ("unknown_b3", e.unknown_b3 == b.unknown_b3),
                ("flag_200", e.flag_200 == b.flag_200),
                ("save_game_relevant", e.save_game_relevant == b.save_game_relevant),
                ("insert_type", e.insert_type == b.insert_type),
                ("render alpha/view range", e.render_alpha_view_range == b.render_alpha_view_range),
                ("cache_in_range", e.cache_in_range == b.cache_in_range),
                ("data_changed", e.data_changed == b.data_changed),
                ("set classes", e.set_classes(s) == b.set_classes(ts)),
                ("local_box", close(&e.local_box, &b.local_box)),
            ];
            for (what, ok) in checks {
                let x = same.entry(what).or_default();
                if ok {
                    x.0 += 1;
                } else {
                    x.1 += 1;
                    if verbose {
                        println!("{what} differs: {} [{i}] {}", pe.path, s.get(e.name.0 as usize).map_or("", |s| s));
                    }
                }
            }
        }
    }
    let bytes = target.write();
    let back = Lrent::read(&bytes).map_err(|e| format!("written layer does not read back: {e}"))?;
    println!("{built} templates built into {name} ({} entities, {} bytes, reads back)", back.entities().len(), bytes.len());
    for (why, n) in &refused {
        println!("refused {n}: {why}");
    }
    println!("field                    same  differs (vs the first top-level world instance)");
    for (what, (a, b)) in &same {
        println!("{what:24} {a:5} {b:5}");
    }
    Ok(())
}

/// Reads + writes every `ext` file of a pak and compares the bytes.
fn round_trip_check<E: std::fmt::Display>(
    pak: &Path,
    ext: &str,
    round_trip: impl Fn(&[u8]) -> Result<(Vec<u8>, usize), E>,
) -> Result<(), Box<dyn std::error::Error>> {
    let pak = Pak::open(pak)?;
    let (mut ok, mut bad) = (0, 0);
    for e in pak.entries.iter().filter(|e| e.path.to_lowercase().ends_with(ext)) {
        let data = pak.read(e)?;
        match round_trip(&data) {
            Ok((out, _)) if out == data => ok += 1,
            Ok((out, n)) => {
                bad += 1;
                let at = out.iter().zip(&data).position(|(a, b)| a != b).unwrap_or(out.len().min(data.len()));
                println!("DIFF  {} ({n} entities): first difference at 0x{at:X}, sizes {} / {}", e.path, data.len(), out.len());
            }
            Err(err) => {
                bad += 1;
                println!("ERROR {}: {err}", e.path);
            }
        }
    }
    println!("{ok} identical, {bad} failed");
    if bad > 0 {
        return Err("round trip failed".into());
    }
    Ok(())
}

/// `risen copy-check`: imports all movable top-level entities of each layer into a copy of
/// `target`, writes + re-reads it, and compares every copied entity with its original.
fn copy_check(pak: &Pak, target: &Lrent, verbose: bool) -> Result<(), Box<dyn std::error::Error>> {
    use risen_formats::strs::resolved_entity;
    let (mut layers, mut groups, mut entities, mut same, mut differ, mut order_bad) = (0, 0, 0, 0, 0, 0);
    let mut refused: BTreeMap<String, usize> = BTreeMap::new();
    for pe in pak.entries.iter().filter(|e| e.path.to_lowercase().ends_with(".lrent")) {
        let src = Lrent::read(&pak.read(pe)?).map_err(|e| format!("{}: {e}", pe.path))?;
        let Some(sc) = src.context() else { continue };
        let mut roots = Vec::new();
        for i in sc.children(0) {
            match src.check_movable(i) {
                Ok(()) => roots.push(i),
                Err(why) => {
                    // Group the reasons by what couldn't be read.
                    let what = why.split_once("unreadable: ").map_or(why.as_str(), |w| w.1).to_string();
                    if verbose {
                        println!("{} [{i}]: {what}", pe.path);
                    }
                    *refused.entry(what).or_default() += 1;
                }
            }
        }
        if roots.is_empty() {
            continue;
        }
        layers += 1;
        let mut t = target.clone();
        let imported = t.import(&src, &roots).map_err(|e| format!("{}: {e}", pe.path))?;
        let back = Lrent::read(&t.write()).map_err(|e| format!("{}: written layer doesn't read: {e}", pe.path))?;
        // The target's table stays in exact first-use order (new strings appended in walk order).
        let mut seq = Vec::new();
        let mut back2 = back.clone();
        let unknown = back2.walk_strs(&mut |s| seq.push(s.0));
        if unknown.is_empty() {
            let mut seen = vec![false; back.strings().len()];
            let first: Vec<u16> = seq.iter().copied().filter(|&i| !std::mem::replace(&mut seen[i as usize], true)).collect();
            if first.len() != seen.len() || first.windows(2).any(|w| w[0] > w[1]) {
                order_bad += 1;
                println!("ORDER {}: target table not in first-use order after the import", pe.path);
            }
        }
        let bc = back.context().ok_or("target lost its context")?;
        for (imp, &root) in imported.iter().zip(&roots) {
            groups += 1;
            let tree = sc.subtree(root);
            let copy_tree = bc.subtree(imp.index);
            if tree.len() != copy_tree.len() {
                differ += 1;
                println!("DIFF {} [{root}]: subtree {} -> {}", pe.path, tree.len(), copy_tree.len());
                continue;
            }
            for (&j, &k) in tree.iter().zip(&copy_tree) {
                entities += 1;
                let mut e = bc.entities[k].clone();
                for (old, new) in &imp.guids {
                    if e.id.guid == *new {
                        e.id.guid = *old;
                    }
                    for ps in &mut e.property_sets {
                        ps.object.replace_guid(new, old);
                    }
                }
                if resolved_entity(src.strings(), &sc.entities[j]) == resolved_entity(back.strings(), &e) {
                    same += 1;
                } else {
                    differ += 1;
                    println!("DIFF {} [{j}] -> [{k}]", pe.path);
                }
            }
        }
    }
    println!("{layers} layers, {groups} groups / {entities} entities copied: {same} identical, {differ} differ");
    println!("target table out of first-use order: {order_bad} layers");
    println!("refused top-level entities: {}", refused.values().sum::<usize>());
    for (what, n) in &refused {
        println!("  {n:6}  {what}");
    }
    if differ > 0 || order_bad > 0 {
        return Err("copy check failed".into());
    }
    Ok(())
}

/// A GENOMFLE file whose string indices can be walked.
trait StrFile: Sized {
    fn parse(d: &[u8]) -> Result<Self, ParseError>;
    fn bytes(&self) -> Vec<u8>;
    fn walk(&mut self, f: &mut dyn FnMut(&mut StrRef)) -> Vec<Unknown>;
    fn table(&mut self) -> &mut Vec<String>;
}

impl StrFile for Lrent {
    fn parse(d: &[u8]) -> Result<Self, ParseError> {
        Lrent::read(d)
    }
    fn bytes(&self) -> Vec<u8> {
        self.write()
    }
    fn walk(&mut self, f: &mut dyn FnMut(&mut StrRef)) -> Vec<Unknown> {
        self.walk_strs(f)
    }
    fn table(&mut self) -> &mut Vec<String> {
        &mut self.archive.strings
    }
}

impl StrFile for Tple {
    fn parse(d: &[u8]) -> Result<Self, ParseError> {
        Tple::read(d)
    }
    fn bytes(&self) -> Vec<u8> {
        self.write()
    }
    fn walk(&mut self, f: &mut dyn FnMut(&mut StrRef)) -> Vec<Unknown> {
        self.walk_strs(f)
    }
    fn table(&mut self) -> &mut Vec<String> {
        &mut self.archive.strings
    }
}

#[derive(Default)]
struct StrCheck {
    files: usize,
    complete: usize,
    refs: usize,
    out_of_range: usize,
    /// Unknown site -> (sites, files).
    unknown: BTreeMap<Unknown, (usize, usize)>,
    /// Table entries never reached: in fully walked files / in all files.
    unreached_complete: usize,
    unreached_all: usize,
    /// First uses of the reached entries come in table order (gaps allowed).
    first_use_monotonic: usize,
    /// ... and without gaps (the table is exactly the first-use order).
    first_use_exact: usize,
    /// Fully walked files whose table is NOT the exact first-use order = a missed or misplaced site.
    complete_not_exact: Vec<String>,
    remap_ok: usize,
    remap_failed: usize,
}

impl StrCheck {
    fn pak<F: StrFile>(&mut self, pak: &Path, ext: &str, verbose: bool) -> Result<(), Box<dyn std::error::Error>> {
        let pak = Pak::open(pak)?;
        for pe in pak.entries.iter().filter(|e| e.path.to_lowercase().ends_with(ext)) {
            let data = pak.read(pe)?;
            let mut file = F::parse(&data).map_err(|e| format!("{}: {e}", pe.path))?;
            self.file(&mut file, &data, &pe.path, verbose)?;
        }
        Ok(())
    }

    fn file<F: StrFile>(&mut self, file: &mut F, data: &[u8], path: &str, verbose: bool) -> Result<(), String> {
        let mut seq = Vec::new();
        let unknown = file.walk(&mut |s| seq.push(s.0));
        let table = file.table().clone();
        let n = table.len();
        self.files += 1;
        self.refs += seq.len();
        self.out_of_range += seq.iter().filter(|&&i| i as usize >= n).count();

        let mut seen = vec![false; n];
        let mut first = Vec::new();
        for &i in seq.iter().filter(|&&i| (i as usize) < n) {
            if !std::mem::replace(&mut seen[i as usize], true) {
                first.push(i as usize);
            }
        }
        let unreached = n - first.len();
        self.unreached_all += unreached;
        if first.windows(2).all(|w| w[0] < w[1]) {
            self.first_use_monotonic += 1;
            if unreached == 0 {
                self.first_use_exact += 1;
            }
        }
        let mut kinds: BTreeMap<&Unknown, usize> = BTreeMap::new();
        for u in &unknown {
            *kinds.entry(u).or_default() += 1;
        }
        for (u, c) in &kinds {
            let e = self.unknown.entry((*u).clone()).or_default();
            e.0 += c;
            e.1 += 1;
        }
        if unknown.is_empty() {
            self.complete += 1;
            self.unreached_complete += unreached;
            if unreached > 0 || !first.windows(2).all(|w| w[0] < w[1]) {
                self.complete_not_exact.push(path.to_string());
            }
        }
        if verbose && (!unknown.is_empty() || unreached > 0) {
            let list: Vec<String> = kinds.iter().map(|(u, c)| format!("{u} x{c}")).collect();
            println!("{path}: {n} strings, {unreached} unreached; {}", list.join(", "));
        }

        // Remap test: reverse the table, read back, compare the resolved strings, reverse again.
        let resolved: Vec<&str> = seq.iter().map(|&i| table.get(i as usize).map_or("", |s| s.as_str())).collect();
        let rev = |s: &mut StrRef| {
            if (s.0 as usize) < n {
                s.0 = (n - 1 - s.0 as usize) as u16;
            }
        };
        file.walk(&mut |s| rev(s));
        file.table().reverse();
        let mut back = F::parse(&file.bytes()).map_err(|e| format!("{path}: remapped file doesn't parse: {e}"))?;
        let table2 = back.table().clone();
        let mut seq2 = Vec::new();
        back.walk(&mut |s| seq2.push(s.0));
        let resolved2: Vec<&str> = seq2.iter().map(|&i| table2.get(i as usize).map_or("", |s| s.as_str())).collect();
        back.walk(&mut |s| rev(s));
        back.table().reverse();
        if resolved == resolved2 && back.bytes() == data {
            self.remap_ok += 1;
        } else {
            self.remap_failed += 1;
            println!("REMAP FAILED {path}");
        }
        Ok(())
    }

    fn report(&self) {
        println!(
            "{} files, {} fully walked; {} string refs, {} out of range",
            self.files, self.complete, self.refs, self.out_of_range
        );
        println!(
            "unreached table entries: {} in fully walked files, {} in all files",
            self.unreached_complete, self.unreached_all
        );
        println!(
            "first use in table order: {} files ({} exactly = table is the first-use order)",
            self.first_use_monotonic, self.first_use_exact
        );
        println!("fully walked files with a table not in first-use order: {}", self.complete_not_exact.len());
        for p in self.complete_not_exact.iter().take(20) {
            println!("  {p}");
        }
        println!("remap test (reversed table): {} ok, {} failed", self.remap_ok, self.remap_failed);
        let mut list: Vec<_> = self.unknown.iter().collect();
        list.sort_by_key(|u| std::cmp::Reverse(u.1.0));
        println!("not walked yet ({} kinds): sites, files", list.len());
        for (u, (sites, files)) in list {
            println!("  {sites:7} {files:5}  {u}");
        }
    }
}

fn main() -> ExitCode {
    match run(Cli::parse()) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::FAILURE
        }
    }
}
