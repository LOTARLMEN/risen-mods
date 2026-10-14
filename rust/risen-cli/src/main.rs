use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use clap::{Parser, Subcommand};
use risen_formats::lrent::{DynamicEntity, Lrent};
use risen_formats::pak::Pak;
use risen_formats::props::{PropertyObject, format_value};
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
    /// Copy an entity (with its children) inside its layer, new GUIDs, and place the copy.
    /// Writes the layer like `move`
    Copy {
        /// Layer name / path inside projects.pak (Wild_Outdoor_Monster_01), or a .lrent file
        layer: String,
        /// Entity GUID or index in the layer (see `risen entities`)
        entity: String,
        /// World position of the copy x y z
        #[arg(long, num_args = 3, allow_negative_numbers = true, conflicts_with = "by", required_unless_present = "by")]
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
    /// template (in its own layer, so property data is reused as is) and moves the copy there
    Spawn {
        /// Template name (It_Plant_Mushroom, Wolf) or GUID
        template: String,
        /// World position x y z (y = up)
        #[arg(long, num_args = 3, allow_negative_numbers = true, required = true)]
        to: Vec<f32>,
        /// Use an instance from this layer (default: the layer with the most instances)
        #[arg(long)]
        layer: Option<String>,
        /// Number of copies, spread on a circle of --spread radius around --to
        #[arg(long, default_value_t = 1)]
        count: usize,
        #[arg(long, default_value_t = 150.0)]
        spread: f32,
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
        Cmd::Copy { layer, entity, to, by, count, out, dry_run } => {
            let game = game_dir(cli.game)?;
            let pak = Pak::open(projects_pak(&game))?;
            let mut l = open_layer_for_edit(&game, &pak, &layer)?;
            let i = l.entity_index(&entity)?;
            let from = l.lrent.entities()[i].position();
            println!("{}", l.src);
            println!("copy {}", l.describe(i));
            for k in 1..=count {
                let d = match (&to, &by) {
                    (Some(t), _) => [t[0] - from[0], t[1] - from[1], t[2] - from[2]],
                    (None, Some(b)) => [b[0] * k as f32, b[1] * k as f32, b[2] * k as f32],
                    (None, None) => unreachable!(),
                };
                let n = l.copy(i, d)?;
                println!("  -> {}", l.describe(n));
            }
            if !dry_run {
                l.save(out)?;
            }
        }
        Cmd::Spawn { template, to, layer, count, spread, out, dry_run } => {
            let game = game_dir(cli.game)?;
            let names = template_names(&game)?;
            let want = template.trim_matches(['{', '}']).to_lowercase();
            let guid = names
                .iter()
                .find(|(g, n)| n.to_lowercase() == want || g.trim_matches(['{', '}']).to_lowercase() == want)
                .map(|(g, _)| g.clone())
                .ok_or_else(|| format!("{template}: no such template (see `risen templates`)"))?;
            let pak = Pak::open(projects_pak(&game))?;
            // Layer with an instance (top-level, i.e. direct child of the layer root) of the template.
            let layer_filter = layer.as_ref().map(|l| l.to_lowercase().trim_end_matches(".lrent").to_string());
            let mut best: Option<(usize, String)> = None;
            for pe in pak.entries.iter().filter(|e| e.path.to_lowercase().ends_with(".lrent")) {
                let lname = pe.path.rsplit('\\').next().unwrap_or(&pe.path).trim_end_matches(".lrent").to_lowercase();
                if layer_filter.as_ref().is_some_and(|f| *f != lname) {
                    continue;
                }
                let l = Lrent::read(&pak.read(pe)?).map_err(|e| format!("{}: {e}", pe.path))?;
                let n = instances(&l, &guid).len();
                if n > 0 && best.as_ref().is_none_or(|b| n > b.0) {
                    best = Some((n, pe.path.clone()));
                }
            }
            let (_, layer_path) = best.ok_or_else(|| match &layer {
                Some(l) => format!("no top-level instance of {template} in layer {l}"),
                None => format!("no top-level instance of {template} anywhere in the world"),
            })?;
            let mut l = open_layer_for_edit(&game, &pak, &layer_path)?;
            let i = *instances(&l.lrent, &guid).first().ok_or("instance vanished from the loose file")?;
            let from = l.lrent.entities()[i].position();
            println!("{}", l.src);
            println!("source {}", l.describe(i));
            for k in 0..count {
                let a = k as f32 / count as f32 * std::f32::consts::TAU;
                let r = if count > 1 { spread } else { 0.0 };
                let d = [to[0] + r * a.cos() - from[0], to[1] - from[1], to[2] + r * a.sin() - from[2]];
                let n = l.copy(i, d)?;
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
        Cmd::Templates { filter, props, json } => {
            let pak = Pak::open(templates_pak(&game_dir(cli.game)?))?;
            let needle = filter.map(|f| f.to_lowercase().trim_matches(['{', '}']).to_string());
            let mut list = Vec::new();
            for pe in pak.entries.iter().filter(|e| e.path.to_lowercase().ends_with(".tple")) {
                let t = Tple::read(&pak.read(pe)?).map_err(|e| format!("{}: {e}", pe.path))?;
                // Multi-templates are listed by their root entity (named like the file).
                let Some(e) = t.template().or(t.entities.first()) else { continue };
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

fn main() -> ExitCode {
    match run(Cli::parse()) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::FAILURE
        }
    }
}
