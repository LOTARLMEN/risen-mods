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
use risen_formats::tple::Tple;
use serde::Serialize;

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
    /// List the files of a .pak: stored size, encryption+compression, path
    PakList {
        pak: PathBuf,
        /// Only paths containing this text (case-insensitive)
        filter: Option<String>,
    },
    /// Unpack one file (by name or path inside the pak) to OUT
    PakExtract { pak: PathBuf, name: String, out: PathBuf },
    /// List world layers (.lrent) in data\common\projects.pak
    Layers {
        /// Only paths containing this text (case-insensitive)
        filter: Option<String>,
    },
    /// List the entities of a layer: name, position, property set classes
    Entities {
        /// Layer: a name / path inside projects.pak (Harbour_Town_NPC_01) or a .lrent file
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
    /// Find entities by name (substring, case-insensitive) or GUID in every layer of projects.pak
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
        /// Template name (It_Plant_Mushroom, Wolf) or GUID
        template: String,
        /// World position x y z (y = up)
        #[arg(long, num_args = 3, allow_negative_numbers = true, required = true)]
        to: Vec<f32>,
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
    /// List templates (data\common\templates.pak): name, GUID, property set classes, path
    Templates {
        /// Only names / paths containing this text (case-insensitive), or a template GUID
        filter: Option<String>,
        /// Print every property of every property set
        #[arg(long)]
        props: bool,
        #[arg(long)]
        json: bool,
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
fn template_names(game: &Path) -> Result<HashMap<String, String>, Box<dyn std::error::Error>> {
    let pak = Pak::open(templates_pak(game))?;
    let mut map = HashMap::new();
    for pe in pak.entries.iter().filter(|e| e.path.to_lowercase().ends_with(".tple")) {
        let t = Tple::read(&pak.read(pe)?).map_err(|e| format!("{}: {e}", pe.path))?;
        if let Some(e) = t.template() {
            map.insert(e.id.text(), t.name(e).to_string());
        }
    }
    Ok(map)
}

/// The template with this GUID ("{...}", upper case) and its path in templates.pak.
fn load_template(game: &Path, guid: &str) -> Result<(Tple, String), Box<dyn std::error::Error>> {
    let pak = Pak::open(templates_pak(game))?;
    for pe in pak.entries.iter().filter(|e| e.path.to_lowercase().ends_with(".tple")) {
        let t = Tple::read(&pak.read(pe)?).map_err(|e| format!("{}: {e}", pe.path))?;
        if t.template().is_some_and(|e| e.id.text() == guid) {
            return Ok((t, pe.path.clone()));
        }
    }
    Err(format!("template {guid} not found").into())
}

/// Mesh paks for the local bounding box of a new entity built from a template.
struct MeshPaks {
    meshes: Pak,
    animations: Pak,
}

impl MeshPaks {
    fn open(game: &Path) -> Result<MeshPaks, Box<dyn std::error::Error>> {
        Ok(MeshPaks {
            meshes: Pak::open(game.join("data").join("common").join("meshes.pak"))?,
            animations: Pak::open(game.join("data").join("compiled").join("animations.pak"))?,
        })
    }

    /// The `Boundary` of the template's mesh (eCMesh_PS) or animated model (eCAnimation_PS), as world
    /// entities store it; else a 1 m cube. Returns the box and where it came from.
    fn local_box(&self, t: &Tple) -> Result<([f32; 6], String), Box<dyn std::error::Error>> {
        let te = t.template().ok_or("multi-template")?;
        let s = t.strings();
        let sources = [
            ("eCMesh_PS", "MeshFileName", &self.meshes, "_xmsh"),
            ("eCAnimation_PS", "ResourceFilePath", &self.animations, "_xmac"),
        ];
        for (class, prop, pak, ext) in sources {
            let Some(p) =
                te.sets.iter().find(|x| x.object.class(s) == class).and_then(|x| x.object.body.as_ref()?.props.get(s, prop))
            else {
                continue;
            };
            let Some(file) = p.value.get(..2).and_then(|v| s.get(u16::from_le_bytes([v[0], v[1]]) as usize)) else {
                continue;
            };
            if let Some(e) = pak.find(&risen_formats::resource::compiled_name(file, ext))
                && let Some(b) = risen_formats::resource::boundary(&pak.read(e)?)
            {
                return Ok((b, e.path.clone()));
            }
        }
        Ok(([-50.0, -50.0, -50.0, 50.0, 50.0, 50.0], "default 1 m cube (no mesh bounds found)".into()))
    }
}

/// Loose override of a projects.pak file: data\common\projects\<path in pak>.
fn loose_path(game: &Path, pak_path: &str) -> PathBuf {
    pak_path.split('\\').fold(game.join("data").join("common").join("projects"), |p, part| p.join(part))
}

/// A layer opened for editing: the loose file if one exists (earlier edits), else the pak copy.
struct EditLayer {
    /// Where it was read from, for messages
    src: String,
    /// Where edits are written (the loose file)
    loose: PathBuf,
    lrent: Lrent,
}

fn open_layer_for_edit(game: &Path, pak: &Pak, layer: &str) -> Result<EditLayer, Box<dyn std::error::Error>> {
    // A .lrent file on disk is edited in place (unless --out).
    if Path::new(layer).is_file() {
        let lrent = Lrent::read(&std::fs::read(layer)?).map_err(|e| format!("{layer}: {e}"))?;
        return Ok(EditLayer { src: layer.to_string(), loose: PathBuf::from(layer), lrent });
    }
    let name = if layer.to_lowercase().ends_with(".lrent") { layer.to_string() } else { format!("{layer}.lrent") };
    let pe = pak.find(&name).ok_or_else(|| format!("{name}: not in projects.pak (see `risen layers`)"))?;
    let loose = loose_path(game, &pe.path);
    let (src, data) = if loose.is_file() {
        (loose.display().to_string(), std::fs::read(&loose)?)
    } else {
        (format!("projects.pak:{}", pe.path), pak.read(pe)?)
    };
    let lrent = Lrent::read(&data).map_err(|e| format!("{src}: {e}"))?;
    Ok(EditLayer { src, loose, lrent })
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
fn load_layer(game: Option<PathBuf>, spec: &str) -> Result<(String, Lrent), Box<dyn std::error::Error>> {
    let p = Path::new(spec);
    let data = if p.is_file() {
        (spec.to_string(), std::fs::read(p)?)
    } else {
        let pak = Pak::open(projects_pak(&game_dir(game)?))?;
        let name = if spec.to_lowercase().ends_with(".lrent") { spec.to_string() } else { format!("{spec}.lrent") };
        let e = pak.find(&name).ok_or_else(|| format!("{name}: not in projects.pak (see `risen layers`)"))?;
        (e.path.clone(), pak.read(e)?)
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
    /// Name of the creator template (templates.pak)
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
            let pak = Pak::open(projects_pak(&game_dir(cli.game)?))?;
            let filter = filter.map(|f| f.to_lowercase());
            for e in pak.entries.iter().filter(|e| e.path.to_lowercase().ends_with(".lrent")) {
                if filter.as_ref().is_none_or(|f| e.path.to_lowercase().contains(f)) {
                    println!("{:9} {}", e.size, e.path);
                }
            }
        }
        Cmd::Entities { layer, props, geometry, json } => {
            let templates = game_dir(cli.game.clone()).ok().and_then(|g| template_names(&g).ok()).unwrap_or_default();
            let (path, l) = load_layer(cli.game, &layer)?;
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
            let templates = template_names(&game)?;
            let pak = Pak::open(projects_pak(&game))?;
            let needle = text.to_lowercase();
            let needle = needle.trim_matches(['{', '}']);
            let mut list = Vec::new();
            for pe in pak.entries.iter().filter(|e| e.path.to_lowercase().ends_with(".lrent")) {
                let l = Lrent::read(&pak.read(pe)?).map_err(|e| format!("{}: {e}", pe.path))?;
                let s = l.strings();
                for (i, e) in l.entities().iter().enumerate() {
                    let name = s.get(e.name.0 as usize).map_or(String::new(), |n| n.to_lowercase());
                    let guid = e.id.text().to_lowercase();
                    if name.contains(needle) || guid.trim_matches(['{', '}']) == needle {
                        let mut o = entity_out(i, e, s, props, &templates);
                        o.parent = parent_of(&l, i);
                        o.layer = Some(pe.path.clone());
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
            let pak = Pak::open(projects_pak(&game))?;
            let mut l = open_layer_for_edit(&game, &pak, &layer)?;
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
            let pak = Pak::open(projects_pak(&game))?;
            let mut l = open_layer_for_edit(&game, &pak, &layer)?;
            let i = l.entity_index(&entity)?;
            let from = l.lrent.entities()[i].position();
            println!("{}", l.src);
            println!("copy {}", l.describe(i));
            // --into the same layer = an ordinary copy.
            let mut target = match &into {
                Some(t) => Some(open_layer_for_edit(&game, &pak, t)?).filter(|t| t.loose != l.loose),
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
        Cmd::Spawn { template, to, layer, count, spread, from_template, out, dry_run } => {
            let game = game_dir(cli.game)?;
            let names = template_names(&game)?;
            let want = template.trim_matches(['{', '}']).to_lowercase();
            let guid = names
                .iter()
                .find(|(g, n)| n.to_lowercase() == want || g.trim_matches(['{', '}']).to_lowercase() == want)
                .map(|(g, _)| g.clone())
                .ok_or_else(|| format!("{template}: no such template (see `risen templates`)"))?;
            let pak = Pak::open(projects_pak(&game))?;
            // Source instance (top-level, i.e. direct child of the layer root): from the target
            // layer when it has one, else from the layer with the most instances.
            let target = layer.as_ref().map(|t| open_layer_for_edit(&game, &pak, t)).transpose()?;
            // Position of copy k: on a circle around --to.
            let place = |k: usize| {
                let a = k as f32 / count as f32 * std::f32::consts::TAU;
                let r = if count > 1 { spread } else { 0.0 };
                [to[0] + r * a.cos(), to[1], to[2] + r * a.sin()]
            };
            let mut source = None;
            let mut from_template = from_template;
            if !from_template && target.as_ref().is_none_or(|t| instances(&t.lrent, &guid).is_empty()) {
                let mut best: Option<(usize, String)> = None;
                for pe in pak.entries.iter().filter(|e| e.path.to_lowercase().ends_with(".lrent")) {
                    let l = Lrent::read(&pak.read(pe)?).map_err(|e| format!("{}: {e}", pe.path))?;
                    let n = instances(&l, &guid).len();
                    if n > 0 && best.as_ref().is_none_or(|b| n > b.0) {
                        best = Some((n, pe.path.clone()));
                    }
                }
                match best {
                    Some((_, path)) => {
                        let s = open_layer_for_edit(&game, &pak, &path)?;
                        // (The target's loose file may have lost the instances its pak copy has.)
                        if target.as_ref().is_none_or(|t| t.loose != s.loose) {
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
                let (t, path) = load_template(&game, &guid)?;
                let (local_box, how) = MeshPaks::open(&game)?.local_box(&t)?;
                println!("template {path}, local box from {how}");
                println!("into {}", l.src);
                for k in 0..count {
                    let n = l.lrent.spawn_template(&t, place(k), local_box)?;
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
                println!("  -> {}", l.describe(n));
            }
            if !dry_run {
                l.save(out)?;
            }
        }
        Cmd::Delete { layer, entity, out, dry_run } => {
            let game = game_dir(cli.game)?;
            let pak = Pak::open(projects_pak(&game))?;
            let mut l = open_layer_for_edit(&game, &pak, &layer)?;
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
                for pe in pak.entries.iter().filter(|e| e.path.to_lowercase().ends_with(".lrent")) {
                    let loose = loose_path(&game, &pe.path);
                    if loose == l.loose {
                        continue;
                    }
                    let data = if loose.is_file() { std::fs::read(&loose)? } else { pak.read(pe)? };
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
        Cmd::Templates { filter, props, json } => {
            let pak = Pak::open(templates_pak(&game_dir(cli.game)?))?;
            let needle = filter.map(|f| f.to_lowercase().trim_matches(['{', '}']).to_string());
            let mut list = Vec::new();
            for pe in pak.entries.iter().filter(|e| e.path.to_lowercase().ends_with(".tple")) {
                let t = Tple::read(&pak.read(pe)?).map_err(|e| format!("{}: {e}", pe.path))?;
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
                        path: pe.path.clone(),
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
    let mesh = MeshPaks::open(game)?;
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
